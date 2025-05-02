//
// Created by kj16609 on 7/23/24.
//

#include "ServerContext.hpp"

#include <spdlog/async_logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <filesystem>

#include "ConnectionArguments.hpp"
#include "NET_CONSTANTS.hpp"
#include "api/helpers/ResponseCallback.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "db/setup/ManagementConnection.hpp"
#include "drogon/HttpAppFramework.h"
#include "logging/log.hpp"

namespace idhan
{

void ServerContext::setupCORSSupport() const
{
	drogon::app().registerPreRoutingAdvice(
		[ this ](
			const drogon::HttpRequestPtr& request, drogon::FilterCallback&& stop, drogon::FilterChainCallback&& pass )
		{
			// if ( args.testmode ) log::info( "Handling query: {}:{}", request->getMethodString(), request->getPath() );

			if ( !request->path().starts_with( "/hyapi" ) || request->method() != drogon::Options )
			{
				pass();
				return;
			}

			const auto response { drogon::HttpResponse::newHttpResponse() };

			response->addHeader( "Access-Control-Allow-Headers", "*" );
			response->addHeader( "Access-Control-Allow-Origin", "*" );
			response->addHeader( "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS, HEAD" );

			stop( response );
		} );

	drogon::app().registerPostHandlingAdvice(
		[]( [[maybe_unused]] const drogon::HttpRequestPtr& request, const drogon::HttpResponsePtr& response )
		{
			response->addHeader( "Access-Control-Allow-Headers", "*" );
			response->addHeader( "Access-Control-Allow-Origin", "*" );
			response->addHeader( "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS, HEAD" );
		} );
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

	// stdout log disabled
	if ( !arguments.use_stdout )
	{
		auto file_logger { spdlog::rotating_logger_mt( "file_logger", "./log/info.log", 1048576 * 10, 512, true ) };
		file_logger->set_level( spdlog::level::info );

		file_logger->set_pattern( std::string( server_format_str ) );

		spdlog::set_default_logger( file_logger );
		trantor::Logger::enableSpdLog( file_logger );

		return file_logger;
	}
	else
	{
		auto file_logger {
			std::make_shared< spdlog::sinks::rotating_file_sink_mt >( "./log/info.log", 1048576 * 10, 512, true )
		};

		auto stdout_logger { std::make_shared< spdlog::sinks::stdout_color_sink_mt >() };

		auto logger {
			std::make_shared< spdlog::logger >( "multi_sink", spdlog::sinks_init_list { stdout_logger, file_logger } )
		};

		logger->set_pattern( std::string( server_format_str ) );

#ifndef NDEBUG
		// file_logger->set_level( spdlog::level::info );
		// stdout_logger->set_level( spdlog::level::debug );
		// logger->set_level( spdlog::level::debug );
#endif

		// spdlog::set_default_logger( logger );
		trantor::Logger::enableSpdLog( logger );
		return logger;
	}
}

ServerContext::ServerContext( const ConnectionArguments& arguments ) :
  m_logger( createLogger( arguments ) ),
  m_postgresql_management( std::make_unique< ManagementConnection >( arguments ) ),
  args( arguments )
{
	log::server::info( "IDHAN initialization starting" );

	// spdlog::enable_backtrace( 32 );

	log::debug( "Logging show debug" );
	log::info( "Logging show info" );

	std::size_t hardware_count { std::min( static_cast< std::size_t >( std::thread::hardware_concurrency() ), 4ul ) };
	std::size_t rest_count { hardware_count / 4 };
	std::size_t db_count { hardware_count };

	constexpr std::string_view log_directory { "./log/drogon" };

	std::filesystem::create_directories( log_directory );

	drogon::app()
		.setLogPath( "./" )
		.setLogLevel( trantor::Logger::kInfo )
		.addListener( "127.0.0.1", IDHAN_DEFAULT_PORT )
		.setThreadNum( rest_count )
		.setClientMaxBodySize( std::numeric_limits< std::uint64_t >::max() )
		.setDocumentRoot( "./pages" )
		.setExceptionHandler( exceptionHandler )
		.setLogPath( std::string( log_directory ), "", 1024 * 1024 * 1024, 8, true );

	drogon::orm::PostgresConfig config;
	config.host = arguments.hostname;
	config.port = arguments.port;
	config.databaseName = arguments.dbname;
	config.username = arguments.user;
	config.password = arguments.password;
	config.connectionNumber = db_count;
	config.name = "default";
	config.isFast = false;
	config.characterSet = "UTF-8";
	config.timeout = 60.0f;
	config.autoBatch = false;

	if ( arguments.testmode )
	{
		log::warn( "Connecting to database using test mode!" );
		config.connectOptions.emplace( std::make_pair( "search_path", "test" ) );
	}

	drogon::app().addDbClient( config );

	setupCORSSupport();

	log::server::info( "IDHAN initialization finished" );
}

void trantorHook( const char* msg, const std::uint64_t len )
{
	log::info( "Hook: {}", std::string_view( msg, len ) );
}

void ServerContext::run()
{
	log::server::info( "Starting runtime" );

#ifndef NDEBUG
	trantor::Logger::setOutputFunction( trantorHook, []() {} );
#endif

	log::info( "Server available at http://localhost:{}", IDHAN_DEFAULT_PORT );
	log::info( "Swagger docs available at http://localhost:{}/api", IDHAN_DEFAULT_PORT );

	drogon::app().run();

	// log::server::info( "Shutting down" );
	return;
}

ServerContext::~ServerContext()
{}

} // namespace idhan
