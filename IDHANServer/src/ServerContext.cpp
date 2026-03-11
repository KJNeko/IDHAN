//
// Created by kj16609 on 7/23/24.
//

#include "ServerContext.hpp"

#include <spdlog/async_logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <filesystem>
#include <fstream>
#include <paths.hpp>
#include <ranges>

#include "ConnectionArguments.hpp"
#include "NET_CONSTANTS.hpp"
#include "api/helpers/ResponseCallback.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "crypto/SHA256.hpp"
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

	drogon::app().registerPostHandlingAdvice(
		[ this ]( [[maybe_unused]] const drogon::HttpRequestPtr& request, const drogon::HttpResponsePtr& response )
		{
			if ( args.testmode )
				log::info( "Finished Handling query: {}:{}", request->getMethodString(), request->getPath() );
			else
				log::debug( "Finished Handling query: {}:{}", request->getMethodString(), request->getPath() );
			addCORSHeaders( response );
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

void printCoreLocation()
{
	if ( std::ifstream ifs( "/proc/sys/kernel/core_pattern" ); ifs )
	{
		std::string loc {};
		ifs >> loc;
		log::info( "Core dumps located at: {}", loc );
	}
}

std::shared_ptr< spdlog::logger > ServerContext::createLogger( const ConnectionArguments& arguments )
{
	constexpr std::string_view server_format_str { "[%Y-%m-%d %H:%M:%S.%e] [SERVER] [%^%l%$] [thread %t] %v" };

	if ( !arguments.use_stdout ) log::warn( "use_stdout is false, This will be the last IDHAN output via stdout!" );

	constexpr std::size_t KiB { 1024 };
	constexpr std::size_t MiB { KiB * 1024 };

	const std::filesystem::path log_path { config::getSilentDefault< std::string >( "logging", "path", "./log" ) };

	// logs all info & errors to a specific file
	auto info_file_logger {
		std::make_shared< spdlog::sinks::rotating_file_sink_mt >( log_path / "info.log", MiB * 2, 4, true )
	};

	info_file_logger->set_pattern( std::string( server_format_str ) );
	info_file_logger->set_level( spdlog::level::info );

	// logs all errors to a specific file
	auto error_file_logger {
		std::make_shared< spdlog::sinks::rotating_file_sink_mt >( log_path / "error.log", MiB * 16, 4, true )
	};

	error_file_logger->set_pattern( std::string( server_format_str ) );
	error_file_logger->set_level( spdlog::level::err );

	// stdout log disabled
	if ( !arguments.use_stdout )
	{
		auto logger { std::make_shared< spdlog::logger >(
			"file_loggers", spdlog::sinks_init_list { info_file_logger, error_file_logger } ) };

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

void setupTempPath()
{
	const std::filesystem::path tmp_path {
		config::getSilentDefault< std::string >( "server", "temp_path", "/tmp/idhan" )
	};

	// create marker
	constexpr std::string_view marker_file { "idhan.active" };
	const auto marker_path { tmp_path / marker_file };

	if ( std::filesystem::exists( tmp_path ) )
	{
		// it exists. can we find out marker?
		if ( std::ifstream ifs( marker_path ); ifs )
		{
			__pid_t pid;
			ifs >> pid;

			// check if the PID still exists
			if ( 0 == kill( pid, 0 ) )
			{
				log::critical(
					"Marker remains from previous IDHAN instance at {}, Delete it only if you are sure the old instance is dead (PID {} is still alive)",
					marker_path.string(),
					pid );
				std::abort();
			}
			else
			{
				log::warn(
					"The marker file found at {} indicates that IDHAN should be running as PID {}, But there was no process with that PID! May indicate a bad shutdown",
					marker_path.string(),
					pid );
			}
			// if kill returns non-zero then the pid likely does not exist.
		}

		//no marker?
	}

	std::filesystem::create_directories( tmp_path );

	if ( std::ofstream ofs( marker_path ); ofs )
	{
		ofs << getpid();
	}
	else
	{
		log::critical( "IDHAN could not create it's running marker at {}", marker_path.string() );
		std::abort();
	}

	auto& app = drogon::app();
	app.setUploadPath( tmp_path );
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
	printCoreLocation();

	std::size_t config_threads { config::getSilentDefault< std::size_t >( "server", "io_threads", 0 ) };
	if ( config_threads == 0 ) config_threads = std::thread::hardware_concurrency();
	std::size_t hardware_count { std::max( config_threads, 2ul ) };
	std::size_t io_threads { hardware_count };

	log::info( "IO Threads: {}", io_threads );

	const std::string log_directory { config::getSilentDefault< std::string >( "logging", "path", "./log" ) };

	std::filesystem::create_directories( log_directory );

	auto& app = drogon::app();

	app.setLogLevel( trantor::Logger::kInfo );
	app.setThreadNum( io_threads );
	app.setClientMaxBodySize( std::numeric_limits< std::uint64_t >::max() );
	app.setDocumentRoot( getStaticPath() );
	app.setExceptionHandler( exceptionHandler );
	app.setLogPath( std::string( log_directory ), "", 1024 * 1024 * 4, 8, true );

	setupTempPath();

	app.registerCustomExtensionMime( "wasm", "application/wasm" );

	app.setFileTypes( { "html", "wasm", "svg", "js", "png", "jpg" } );

	const bool use_tls { config::get< bool >( "host", "use_tls", false ) };

	const auto ipv4_listener { config::get< std::string >( "host", "ipv4_listen", "127.0.0.1" ) };
	const auto ipv6_listener { config::get< std::string >( "host", "ipv6_listen", "::1" ) };

	const auto server_cert_path {
		config::get< std::string, config::no_warn_on_default >( "host", "server_cert_path", "./server.crt" )
	};

	const auto server_key_path {
		config::get< std::string, config::no_warn_on_default >( "host", "server_key_path", "./server.key" )
	};

	if ( !ipv4_listener.empty() )
		app.addListener( ipv4_listener, IDHAN_DEFAULT_PORT, use_tls, server_cert_path, server_key_path );

	if ( !ipv6_listener.empty() )
		app.addListener( ipv6_listener, IDHAN_DEFAULT_PORT, use_tls, server_cert_path, server_key_path );

	drogon::orm::PostgresConfig config {};
	config.host = arguments.hostname;
	config.port = arguments.port;
	config.databaseName = arguments.dbname;
	config.username = arguments.user;
	config.password = arguments.password;
	config.connectionNumber = io_threads / 2;
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

	config.isFast = true;
	config.name = "default";
	config.connectionNumber = 1;

	drogon::app().addDbClient( config );

	setupCORSSupport();

	m_module_loader = std::make_unique< modules::ModuleLoader >();

	m_clusters = std::make_unique< filesystem::ClusterManager >();
	// Register callback to initialize clusters after event loop starts

	log::info( "Thumbnails location: {}", getThumbnailsPath().string() );

	drogon::app().registerBeginningAdvice(
		[ this ]()
		{
			drogon::sync_wait(
				[ this ]() -> drogon::Task< void >
				{
					const auto db { drogon::app().getDbClient() };
					co_await m_clusters->reloadClusters( db );
					co_return;
				}() );

			log::info( "IDHAN initialization finished" );
			log::info( "Server available at http://localhost:{}", IDHAN_DEFAULT_PORT );
			log::info( "Swagger docs available at http://localhost:{}/api", IDHAN_DEFAULT_PORT );
		} );

	drogon::app().registerBeginningAdvice(
		[]()
		{
			drogon::sync_wait(
				[]() -> drogon::Task< void >
				{
					const auto db { drogon::app().getDbClient() };
					const auto key_count_search { co_await db->execSqlCoro( "SELECT count(*) FROM auth_keys" ) };

					const auto key_count {
						key_count_search.empty() ? 0 : key_count_search[ 0 ][ 0 ].as< std::size_t >()
					};

					if ( key_count == 0 )
					{
						// no key, Create a starter one.
						log::warn(
							"No API keys found, One will be generated at first navigation to /generate_api_key" );
					}

					co_return;
				}() );
		} );
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
{
	const auto upload_path { config::getSilentDefault< std::string >( "temp", "path", "/tmp/idhan" ) };
	std::filesystem::remove_all( upload_path );
}

} // namespace idhan
