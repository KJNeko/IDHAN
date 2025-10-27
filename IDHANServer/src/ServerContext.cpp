//
// Created by kj16609 on 7/23/24.
//

#include "ServerContext.hpp"

#include <spdlog/async_logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <filesystem>
#include <paths.hpp>

#include "ConnectionArguments.hpp"
#include "NET_CONSTANTS.hpp"
#include "api/helpers/ResponseCallback.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "db/ManagementConnection.hpp"
#include "drogon/HttpAppFramework.h"
#include "logging/log.hpp"

namespace idhan
{

void addCORSHeaders( const drogon::HttpResponsePtr& response )
{
	response->addHeader( "Access-Control-Allow-Headers", "*" );
	response->addHeader( "Access-Control-Allow-Origin", "*" );
	response->addHeader( "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS, HEAD" );
	response->addHeader( "Access-Control-Max-Age", "86400" );
}

void ServerContext::setupCORSSupport() const
{
	drogon::app().registerPreRoutingAdvice(
		[ this ](
			const drogon::HttpRequestPtr& request, drogon::FilterCallback&& stop, drogon::FilterChainCallback&& pass )
		{
			if ( args.testmode )
				log::info( "Handling query: {}:{}", request->getMethodString(), request->getPath() );
			else
				log::debug( "Handling query: {}:{}", request->getMethodString(), request->getPath() );

			if ( request->method() == drogon::Options )
			{
				const auto response { drogon::HttpResponse::newHttpResponse() };

				addCORSHeaders( response );

				stop( response );

				return;
			}

			pass();
		} );

	drogon::app().registerPostHandlingAdvice( []( [[maybe_unused]] const drogon::HttpRequestPtr& request,
	                                              const drogon::HttpResponsePtr& response )
	                                          { addCORSHeaders( response ); } );
}

void exceptionHandler( const std::exception& e, const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
{
	log::error( "Unhandled exception got to drogon! In request: {} What: {}", request->getQuery(), e.what() );
	spdlog::dump_backtrace();

	auto response { idhan::createInternalError(
		"Unhandled exception got to drogon! In request: {} What: {}", request->getPath(), e.what() ) };

	callback( response );
	// drogon::defaultExceptionHandler( e, request, std::move( callback ) );
}

std::shared_ptr< spdlog::logger > ServerContext::createLogger( const ConnectionArguments& arguments )
{
	constexpr std::string_view server_format_str { "[%Y-%m-%d %H:%M:%S.%e] [SERVER] [%^%l%$] [thread %t] %v" };

	if ( !arguments.use_stdout ) log::warn( "use_stdout is false, This will be the last IDHAN output via stdout!" );

	constexpr std::size_t KiB { 1024 };
	constexpr std::size_t MiB { KiB * 1024 };

	// logs all info & errors to a specific file
	auto info_file_logger {
		std::make_shared< spdlog::sinks::rotating_file_sink_mt >( "./log/info.log", MiB * 2, 4, true )
	};

	info_file_logger->set_pattern( std::string( server_format_str ) );
	info_file_logger->set_level( spdlog::level::info );

	// logs all errors to a specific file
	auto error_file_logger {
		std::make_shared< spdlog::sinks::rotating_file_sink_mt >( "./log/error.log", MiB * 16, 4, true )
	};

	error_file_logger->set_pattern( std::string( server_format_str ) );
	error_file_logger->set_level( spdlog::level::err );

	// stdout log disabled
	if ( !arguments.use_stdout )
	{
		auto logger { std::make_shared<
			spdlog::logger >( "file_loggers", spdlog::sinks_init_list { info_file_logger, error_file_logger } ) };

		logger->set_pattern( std::string( server_format_str ) );

		spdlog::set_default_logger( logger );
		trantor::Logger::enableSpdLog( logger );

		return logger;
	}
	else
	{
		auto stdout_logger { std::make_shared< spdlog::sinks::stdout_color_sink_mt >() };

		auto logger { std::make_shared< spdlog::logger >(
			"multi_sink", spdlog::sinks_init_list { stdout_logger, info_file_logger, error_file_logger } ) };

		logger->set_pattern( std::string( server_format_str ) );

		trantor::Logger::enableSpdLog( logger );
		return logger;
	}
}

ServerContext::ServerContext( const ConnectionArguments& arguments ) :
  m_logger( createLogger( arguments ) ),
  m_postgresql_management( std::make_unique< ManagementConnection >( arguments ) ),
  args( arguments )
{
	log::info( "IDHAN initialization starting" );

	spdlog::enable_backtrace( 32 );

	log::debug( "Logging show debug" );
	log::info( "Logging show info" );

	std::size_t hardware_count { std::min( static_cast< std::size_t >( std::thread::hardware_concurrency() ), 4ul ) };
	std::size_t io_threads { hardware_count };
	std::size_t db_threads { io_threads * 2 };

	log::info( "IO Threads: {}", io_threads );
	log::info( "DB Connections: {}", db_threads );

	constexpr std::string_view log_directory { "./log/drogon" };

	std::filesystem::create_directories( log_directory );

	auto& app = drogon::app()
	                .setLogPath( "./" )
	                .setLogLevel( trantor::Logger::kInfo )
	                .setThreadNum( io_threads )
	                .setClientMaxBodySize( std::numeric_limits< std::uint64_t >::max() )
	                .setDocumentRoot( getStaticPath() )
	                .setExceptionHandler( exceptionHandler )
	                .setLogPath( std::string( log_directory ), "", 1024 * 1024 * 1024, 8, true );

	app.registerCustomExtensionMime( "wasm", "application/wasm" );

	app.setFileTypes( { "html", "wasm", "svg", "js", "png", "jpg" } );

	if ( arguments.listen_localhost_only )
	{
		app.addListener( "127.0.0.1", IDHAN_DEFAULT_PORT );
		app.addListener( "::1", IDHAN_DEFAULT_PORT );
	}
	else
	{
		app.addListener( "0.0.0.0", IDHAN_DEFAULT_PORT );
		app.addListener( "::", IDHAN_DEFAULT_PORT );
	}

	drogon::orm::PostgresConfig config;
	config.host = arguments.hostname;
	config.port = arguments.port;
	config.databaseName = arguments.dbname;
	config.username = arguments.user;
	config.password = arguments.password;
	config.connectionNumber = db_threads;
	config.name = "default";
	config.isFast = false;
	config.characterSet = "UTF-8";
	config.timeout = 60.0f;
	config.autoBatch = false;

	log::info(
		"Connecting to database {} at {}:{} with user {}",
		config.databaseName,
		config.host,
		config.port,
		config.username );

	drogon::app().addDbClient( config );

	setupCORSSupport();

	m_module_loader = std::make_unique< modules::ModuleLoader >();

	m_clusters = std::make_unique< filesystem::ClusterManager >();
	// Register callback to initialize clusters after event loop starts

	drogon::app().getLoop()->runInLoop(
		[ this ]() -> drogon::Task< void > { co_await m_clusters->reloadClusters( drogon::app().getDbClient() ); } );

	drogon::app().registerBeginningAdvice(
		[]()
		{
			log::info( "Server available at http://localhost:{}", IDHAN_DEFAULT_PORT );
			log::info( "Swagger docs available at http://localhost:{}/api", IDHAN_DEFAULT_PORT );
		} );

	log::info( "IDHAN initialization finished" );
}

void trantorHook( const char* msg, const std::uint64_t len )
{
	log::info( "Hook: {}", std::string_view( msg, len ) );
}

void ServerContext::run()
{
	log::info( "Starting runtime" );

	trantor::Logger::setOutputFunction( trantorHook, []() noexcept {} );

	drogon::app().run();

	log::info( "Exiting runtime" );
}

ServerContext::~ServerContext()
{}

} // namespace idhan
