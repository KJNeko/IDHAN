//
// Created by kj16609 on 7/23/24.
//

#include "ServerContext.hpp"

#include <spdlog/async_logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <filesystem>
#include <fstream>
#include <paths.hpp>
#include <ranges>

#include "ConnectionArguments.hpp"
#include "NET_CONSTANTS.hpp"
#include "api/apiPrefixes.hpp"
#include "api/helpers/ResponseCallback.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "crypto/SHA256.hpp"
#include "db/ManagementConnection.hpp"
#include "drogon/HttpAppFramework.h"
#include "filesystem/io/IOUring.hpp"
#include "logging/log.hpp"
#include "mime/MimeDatabase.hpp"
#include "spdlog/async.h"

namespace idhan
{

void addCORSHeaders( const drogon::HttpResponsePtr& response )
{
	response->addHeader( "Access-Control-Allow-Headers", "*" );
	response->addHeader( "Access-Control-Allow-Origin", "*" );
	response->addHeader( "Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS, HEAD" );
	response->addHeader( "Access-Control-Max-Age", "86400" );

	// The wildcard origin above is deliberately not credential-capable: the WebUI is same-origin in
	// production and same-origin via the Vite dev proxy, so its session cookie never needs CORS.
	// Sending credentials would require reflecting a specific origin, because `*` is a literal
	// string rather than a wildcard in credentialed CORS.

	// Isolates cross-origin window references. Unrelated to the WebUI; we open no cross-origin popups.
	response->addHeader( "Cross-Origin-Opener-Policy", "same-origin" );
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

void ServerContext::setupSPAFallback() const
{
	// Drogon routes to controllers first, then to the static file router, which hands anything with
	// an unknown or absent extension to this handler. That makes it the natural home for the SPA's
	// history fallback: API routes have already matched by the time we get here, so serving
	// index.html cannot mask a real API 404.
	drogon::app().setDefaultHandler(
		[]( const drogon::HttpRequestPtr& request, std::function< void( const drogon::HttpResponsePtr& ) >&& callback )
		{
			const auto index_path { getStaticPath() / "index.html" };

			const bool is_navigation { request->method() == drogon::Get || request->method() == drogon::Head };
			const bool wants_html { request->getHeader( "Accept" ).find( "text/html" ) != std::string::npos };

			if ( is_navigation && wants_html && !api::isApiPath( request->path() )
			     && std::filesystem::exists( index_path ) )
			{
				callback( drogon::HttpResponse::newFileResponse( index_path.string() ) );
				return;
			}

			callback( drogon::HttpResponse::newNotFoundResponse( request ) );
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
	constexpr std::string_view server_format_str { "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [thread %t] %v" };

	if ( !arguments.use_stdout ) log::warn( "use_stdout is false, This will be the last IDHAN output via stdout!" );

	constexpr std::size_t KiB { 1024 };
	constexpr std::size_t MiB { KiB * 1024 };

	const std::filesystem::path log_path { config::getLogPath() };

	const std::size_t ring_buffer_size { config::getSilentDefault< std::size_t >( "logging", "buffer_size", 1000000 ) };

	std::vector< spdlog::sink_ptr > sinks {};

	// In-memory ring buffer — captures every level for the /log endpoint
	// Kept as a concrete-typed pointer (see log::setServerLogger) rather than rediscovered later via
	// dynamic_pointer_cast on the logger's type-erased sinks, since that cast has been observed to fail
	// across the module .so boundary on some platforms/spdlog builds.
	auto ring_buffer { std::make_shared< spdlog::sinks::ringbuffer_sink_mt >( ring_buffer_size ) };
	sinks.emplace_back( ring_buffer );
	ring_buffer->set_pattern( std::string( server_format_str ) );
	ring_buffer->set_level( spdlog::level::trace );

	// logs all trace messages to a specific file
	auto& trace_file_logger { sinks.emplace_back(
		std::make_shared< spdlog::sinks::rotating_file_sink_mt >( log_path / "trace.log", MiB * 2, 4, true ) ) };

	trace_file_logger->set_pattern( std::string( server_format_str ) );
	trace_file_logger->set_level( spdlog::level::trace );

	// logs all info & errors to a specific file
	auto& info_file_logger { sinks.emplace_back(
		std::make_shared< spdlog::sinks::rotating_file_sink_mt >( log_path / "info.log", MiB * 2, 4, true ) ) };

	info_file_logger->set_pattern( std::string( server_format_str ) );
	info_file_logger->set_level( spdlog::level::info );

	// logs all errors to a specific file
	auto& error_file_logger { sinks.emplace_back(
		std::make_shared< spdlog::sinks::rotating_file_sink_mt >( log_path / "error.log", MiB * 16, 4, true ) ) };

	error_file_logger->set_pattern( std::string( server_format_str ) );
	error_file_logger->set_level( spdlog::level::warn );

	if ( arguments.use_stdout )
	{
		auto& stdout_logger { sinks.emplace_back( std::make_shared< spdlog::sinks::stdout_color_sink_mt >() ) };
		stdout_logger->set_level( arguments.log_level );
	}

	auto logger { std::make_shared< spdlog::logger >( "default", sinks.begin(), sinks.end() ) };

	logger->set_pattern( std::string( server_format_str ) );
	logger->set_level( spdlog::level::trace );

	spdlog::set_default_logger( logger );
	trantor::Logger::enableSpdLog( logger );
	log::setServerLogger( logger, ring_buffer );

	logger->flush_on( spdlog::level::warn );
	spdlog::flush_every( std::chrono::seconds( 5 ) );

	spdlog::trace( "Trace logging enabled" );
	spdlog::debug( "Debug logging enabled" );
	spdlog::info( "Info logging enabled" );

	return logger;
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

	log::trace( "ServerContext constructor entered" );

	spdlog::enable_backtrace( 32 );

	log::trace( "Backtrace buffer enabled (32 entries)" );
	log::debug( "Logging show debug" );
	log::info( "Logging show info" );
	log::trace( "Calling printCoreLocation" );
	printCoreLocation();
	log::trace( "printCoreLocation completed" );

	std::size_t config_threads { config::getSilentDefault< std::size_t >( "server", "io_threads", 0 ) };
	if ( config_threads == 0 ) config_threads = std::thread::hardware_concurrency();
	std::size_t hardware_count { std::max( config_threads, 2ul ) };
	std::size_t io_threads { hardware_count };

	log::trace( "IO threads calculated: {}", io_threads );
	log::info( "IO Threads: {}", io_threads );

	log::trace( "Configuring drogon app" );
	auto& app = drogon::app();

	app.setLogLevel( trantor::Logger::kInfo );
	app.setThreadNum( io_threads );
	app.setClientMaxBodySize( std::numeric_limits< std::uint64_t >::max() );
	app.setDocumentRoot( getStaticPath() );
	app.setExceptionHandler( exceptionHandler );
	app.setLogPath( std::string( config::getLogPath() ), "", 1024 * 1024 * 4, 8, true );

	log::trace( "Setting up temp path" );
	setupTempPath();
	log::trace( "Temp path setup completed" );

	app.registerCustomExtensionMime( "wasm", "application/wasm" );
	app.registerCustomExtensionMime( "mjs", "text/javascript" );
	app.registerCustomExtensionMime( "webmanifest", "application/manifest+json" );

	// Drogon 404s any extension absent from this list, so it must cover everything a WebUI build
	// emits. `wasm` stays because WebUI plugins may ship it.
	app.setFileTypes( { "html", "css",  "js",   "mjs",  "map",  "json", "txt",  "xml", "webmanifest",
	                    "svg",  "png",  "jpg",  "jpeg", "gif",  "webp", "avif", "ico", "bmp",
	                    "woff", "woff2", "ttf", "otf",  "mp4",  "webm", "wasm" } );

	const bool use_tls { config::get< bool >( "host", "use_tls", false ) };

	const auto ipv4_listener { config::get< std::string >( "host", "ipv4_listen", "127.0.0.1" ) };
	const auto ipv6_listener { config::get< std::string >( "host", "ipv6_listen", "::1" ) };

	const auto server_cert_path {
		config::get< std::string, config::no_warn_on_default >( "host", "server_cert_path", "./server.crt" )
	};

	const auto server_key_path {
		config::get< std::string, config::no_warn_on_default >( "host", "server_key_path", "./server.key" )
	};

	log::trace( "use_tls: {}", use_tls );

	if ( !ipv4_listener.empty() )
	{
		log::trace( "Adding IPv4 listener on {}:{}", ipv4_listener, IDHAN_DEFAULT_PORT );
		app.addListener( ipv4_listener, IDHAN_DEFAULT_PORT, use_tls, server_cert_path, server_key_path );
	}

	if ( !ipv6_listener.empty() )
	{
		log::trace( "Adding IPv6 listener on {}:{}", ipv6_listener, IDHAN_DEFAULT_PORT );
		app.addListener( ipv6_listener, IDHAN_DEFAULT_PORT, use_tls, server_cert_path, server_key_path );
	}

	drogon::orm::PostgresConfig config {
		.host = arguments.hostname,
		.port = arguments.port,
		.databaseName = arguments.dbname,
		.username = arguments.user,
		.password = arguments.password,
		.connectionNumber = std::min( io_threads, std::size_t( 16 ) ),
		.name = "default",
		.isFast = false,
		.characterSet = "UTF-8",
		.timeout = 60.0,
		.autoBatch = false,
		.connectOptions = {}
	};

	if ( arguments.testmode )
	{
		config.connectOptions.insert_or_assign( "search_path", "test" );
	}

	log::trace( "Database config prepared, adding DB client" );
	log::info(
		"Connecting to database {} at {}:{} with user {}",
		config.databaseName,
		config.host,
		config.port,
		config.username );

	drogon::app().addDbClient( config );

	log::trace( "Setting up CORS support" );
	setupCORSSupport();
	setupSPAFallback();
	log::trace( "CORS support configured" );

	m_module_loader = std::make_unique< modules::ModuleLoader >();

	// Must happen before anything can touch FileIOUring (e.g. ClusterManager reading/writing files);
	// IOUring::getInstance() throws if init() was never called.
	IOUring::init();

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
					co_await mime::getMimeDatabase()->reloadMimeParsers();
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
