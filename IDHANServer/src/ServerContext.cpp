#include "ServerContext.hpp"
#include "jobs/JobContext.hpp"
#include "maintenance/HashMaintenance.hpp"
#include "maintenance/TagCountMaintenance.hpp"
#include "maintenance/ModuleMaintenance.hpp"
#include "config/consumeFlag.hpp"

#include <folly/coro/Task.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/thread_factory/NamedThreadFactory.h>
#include <spdlog/async_logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <paths.hpp>
#include <ranges>

#include "ConnectionArguments.hpp"
#include "NET_CONSTANTS.hpp"
#include "auth/authKeys.hpp"
#include "idhan/SHA256.hpp"
#include "db/ManagementConnection.hpp"
#include "downloader/DownloadSessionEventHub.hpp"
#include "downloader/DebugEvents.hpp"
#include "downloader/DownloadSessionManager.hpp"
#include "downloader/RateLimitEventHub.hpp"
#include "embeddings/embeddings.hpp"
#include "filesystem/ThumbnailCache.hpp"
#include "filesystem/io/IOUring.hpp"
#include "httpapi/apiAuth.hpp"
#include "httpapi/clusters.hpp"
#include "httpapi/downloadSessions.hpp"
#include "httpapi/downloaderDebug.hpp"
#include "httpapi/downloaderSecrets.hpp"
#include "httpapi/embeddings.hpp"
#include "httpapi/fileMetadata.hpp"
#include "httpapi/generateApiKey.hpp"
#include "httpapi/health.hpp"
#include "httpapi/heartbeat.hpp"
#include "httpapi/hydrus.hpp"
#include "httpapi/importFile.hpp"
#include "httpapi/info.hpp"
#include "httpapi/jobs.hpp"
#include "httpapi/layouts.hpp"
#include "httpapi/maintenance.hpp"
#include "httpapi/plugins.hpp"
#include "httpapi/rateLimit.hpp"
#include "httpapi/recordFile.hpp"
#include "httpapi/records.hpp"
#include "httpapi/relationships.hpp"
#include "httpapi/search.hpp"
#include "httpapi/tags.hpp"
#include "httpapi/thumbnail.hpp"
#include "httpapi/ui.hpp"
#include "httpapi/version.hpp"
#include "idhan/db/DBConfig.hpp"
#include "idhan/db/DBContext.hpp"
#include "idhan/http/HTTPConfig.hpp"
#include "idhan/http/HTTPServer.hpp"
#include "idhan/http/RouteRegistry.hpp"
#include "httpapi/parameterDeclarations.hpp"
#include "logging/log.hpp"
#include "spdlog/async.h"

namespace idhan
{

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

	std::shared_ptr< spdlog::sinks::ringbuffer_sink_mt > ring_buffer {};
	if ( ring_buffer_size > 0 )
	{
		ring_buffer = std::make_shared< spdlog::sinks::ringbuffer_sink_mt >( ring_buffer_size );
		sinks.emplace_back( ring_buffer );
		ring_buffer->set_pattern( std::string( server_format_str ) );
		ring_buffer->set_level( spdlog::level::trace );
	}
	else
	{
		log::info( "In-memory log ring buffer disabled (logging.buffer_size=0); /log endpoint unavailable" );
	}

	auto& trace_file_logger { sinks.emplace_back(
		std::make_shared< spdlog::sinks::rotating_file_sink_mt >( log_path / "trace.log", MiB * 2, 4, true ) ) };

	trace_file_logger->set_pattern( std::string( server_format_str ) );
	trace_file_logger->set_level( spdlog::level::trace );

	auto& info_file_logger { sinks.emplace_back(
		std::make_shared< spdlog::sinks::rotating_file_sink_mt >( log_path / "info.log", MiB * 2, 4, true ) ) };

	info_file_logger->set_pattern( std::string( server_format_str ) );
	info_file_logger->set_level( spdlog::level::info );

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

	// The marker names the PID of the instance that owns tmp_path.
	constexpr std::string_view marker_file { "idhan.active" };
	const auto marker_path { tmp_path / marker_file };

	if ( std::filesystem::exists( tmp_path ) )
	{
		if ( std::ifstream ifs( marker_path ); ifs )
		{
			__pid_t pid;
			ifs >> pid;

			// kill(pid, 0) succeeds only while that process still exists.
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
		}
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
}

//! URLs a shutdown left unfinished are only recorded in the database, since jobs themselves live in
//! memory and their ids reset on restart. The submits run here rather than inside a database
//! coroutine on purpose: submit() reaches downloader host callbacks that block on their own
//! queries, and those resume on the database executor's single thread, so submitting from that
//! thread deadlocks startup.
static void resumeDownloads( db::DBContext& db, const folly::Executor::KeepAlive<>& db_executor )
{
	auto& downloads { downloader::downloadSessionManager() };
	auto pending { folly::coro::co_withExecutor( db_executor, downloads.resumable( db ) ).start().get() };

	if ( pending.empty() ) return;

	log::info( "Resuming {} interrupted download(s)", pending.size() );

	for ( auto& root : pending )
	{
		auto scheduled { downloads.submit( root.job_id, root.session_id, std::move( root.url ) ) };
		if ( scheduled ) continue;

		folly::coro::co_withExecutor(
			db_executor, downloads.markUnresumable( root.job_id, std::move( scheduled.error() ), db ) )
			.start()
			.get();
	}
}

void ServerContext::startReplacementStack()
{
	log::trace( "Starting replacement HTTP/DB stack (prep only, no routes yet)" );

	db::DBConfig db_config { db::DBConfig::defaults() };
	db_config.host = args.hostname;
	db_config.port = args.port;
	db_config.database = args.dbname;
	db_config.user = args.user;
	db_config.password = args.password;
	db_config.schema = args.schema;
	db_config.extension_schema = args.extension_schema;
	db_config.validate();

	// IDHANDb gets one dedicated thread of its own, isolated from everything else.
	m_db_executor = std::make_shared< folly::CPUThreadPoolExecutor >(
		1, std::make_shared< folly::NamedThreadFactory >( "idhan-db" ) );

	m_db_context = std::make_unique< db::DBContext >( db_config, folly::getKeepAliveToken( m_db_executor.get() ) );

	const auto db_executor { folly::getKeepAliveToken( m_db_executor.get() ) };

	// Runs on IDHANDb's own thread rather than being driven inline on this one (which blockingWait
	// would do); this thread only blocks waiting on the future's result.
	folly::coro::co_withExecutor( db_executor, m_db_context->start() ).start().get();

	log::info( "Replacement DB stack connected to {}:{}", db_config.host, db_config.port );

	const auto marker_recovery { config::consumeFlag( "cluster", "recreate_missing_markers" ) };
	if ( !marker_recovery ) throw std::runtime_error( marker_recovery.error() );

	folly::coro::co_withExecutor(
		db_executor,
		[ this, recreate_markers = *marker_recovery ]() -> runtime::Task<>
		{
			co_await m_clusters->reloadClusters( *m_db_context, recreate_markers );
			co_await embeddings::registerEmbeddingModels( *m_db_context );

			const auto key_count { co_await auth::keyCount( *m_db_context ) };
			if ( key_count == 0 )
				log::warn( "No API keys found, One will be generated at first navigation to /generate_api_key" );
		}() )
		.start()
		.get();

	resumeDownloads( *m_db_context, db_executor );
	m_module_maintenance = std::make_unique< maintenance::ModuleMaintenance >( *m_module_loader );
	m_hash_maintenance = std::make_unique< maintenance::HashMaintenance >( *m_db_context, db_executor );
	m_tag_count_maintenance = std::make_unique< maintenance::TagCountMaintenance >( *m_db_context, db_executor );

	http::HTTPConfig http_config { http::HTTPConfig::defaults() };
	http_config.ipv4_listen = config::get< std::string >( "host", "ipv4_listen", "127.0.0.1" );
	http_config.ipv6_listen = config::get< std::string >( "host", "ipv6_listen", "::1" );
	http_config.port = config::get< std::uint16_t >( "server", "port", IDHAN_DEFAULT_PORT );
	http_config.tls = config::get< bool >( "host", "use_tls", false );
	http_config.self_sign_tls = config::get< bool >( "host", "self_sign_tls", false );
	http_config.certificate =
		config::get< std::string, config::no_warn_on_default >( "host", "server_cert_path", "./server.crt" );
	http_config.private_key =
		config::get< std::string, config::no_warn_on_default >( "host", "server_key_path", "./server.key" );

	http::RouteRegistry routes {};
	m_heartbeat = std::make_unique< httpapi::HeartbeatService >();
	routes.add( httpapi::declareEndpointParameters( m_heartbeat->route() ) );
	routes.add( httpapi::declareEndpointParameters( httpapi::versionRoute() ) );
	routes.add( httpapi::declareEndpointParameters( httpapi::healthRoute() ) );
	routes.add( httpapi::declareEndpointParameters( httpapi::generateApiKeyRoute( *m_db_context ) ) );
	routes.add( httpapi::declareEndpointParameters( httpapi::importFileRoute( *m_db_context ) ) );
	routes.add( httpapi::declareEndpointParameters( httpapi::verifyRoute( *m_db_context ) ) );
	routes.add(
		httpapi::declareEndpointParameters( httpapi::recordFileRoute( *m_db_context, "/records/{record_id}/file" ) ) );
	routes.add(
		httpapi::declareEndpointParameters( httpapi::recordFileRoute( *m_db_context, "/records/{record_id}" ) ) );
	routes.add(
		httpapi::declareEndpointParameters( httpapi::recordInfoRoute( *m_db_context, "/records/{record_id}/info" ) ) );
	routes.add(
		httpapi::declareEndpointParameters(
			httpapi::recordInfoRoute( *m_db_context, "/records/{record_id}/metadata" ) ) );
	routes.add( httpapi::declareEndpointParameters( httpapi::recordMetadataBatchRoute( *m_db_context ) ) );
	routes.add( httpapi::declareEndpointParameters( httpapi::recordMetadataScanRoute( *m_db_context ) ) );
	routes.add( httpapi::declareEndpointParameters( httpapi::thumbnailRoute( *m_db_context ) ) );
	routes.add( httpapi::declareEndpointParameters( httpapi::searchRoute( *m_db_context ) ) );
	routes.add( httpapi::declareEndpointParameters( httpapi::searchPostRoute( *m_db_context ) ) );
	routes.add( httpapi::declareEndpointParameters( httpapi::rateLimitListRoute( *m_db_context ) ) );
	routes.add( httpapi::declareEndpointParameters( httpapi::rateLimitResetRoute( *m_db_context ) ) );
	routes.add( httpapi::declareEndpointParameters( httpapi::rateLimitEventsRoute( *m_db_context ) ) );
	routes.add( httpapi::declareEndpointParameters( httpapi::apiDocsIndexRoute() ) );
	routes.add( httpapi::declareEndpointParameters( httpapi::logRoute( *m_db_context ) ) );
	routes.add( httpapi::declareEndpointParameters( httpapi::pluginsRoute( *m_db_context ) ) );
	routes.add( httpapi::declareEndpointParameters( httpapi::downloaderDebugRoute( *m_db_context ) ) );
	routes.add( httpapi::declareEndpointParameters( httpapi::downloaderDebugEventsRoute( *m_db_context ) ) );
	routes.add( httpapi::declareEndpointParameters( httpapi::downloaderSecretsListRoute( *m_db_context ) ) );
	routes.add( httpapi::declareEndpointParameters( httpapi::downloaderSecretsSetRoute( *m_db_context ) ) );
	for ( auto& route : httpapi::layoutRoutes( *m_db_context ) )
		routes.add( httpapi::declareEndpointParameters( std::move( route ) ) );
	routes.add( httpapi::declareEndpointParameters( httpapi::indexRoute( "/" ) ) );
	routes.add( httpapi::declareEndpointParameters( httpapi::indexRoute( "/status" ) ) );
	// useStaleBuild() fetches this to compare the served entry module against the running one.
	routes.add( httpapi::declareEndpointParameters( httpapi::indexRoute( "/index.html" ) ) );
	routes.add( httpapi::declareEndpointParameters( httpapi::assetsRoute() ) );
	routes.add( httpapi::declareEndpointParameters( httpapi::apiDocsFilesRoute() ) );
	routes.add( httpapi::declareEndpointParameters( httpapi::pluginFilesRoute() ) );
	for ( auto& route : httpapi::tagRoutes( *m_db_context ) )
		routes.add( httpapi::declareEndpointParameters( std::move( route ) ) );
	for ( auto& route : httpapi::relationshipRoutes( *m_db_context ) )
		routes.add( httpapi::declareEndpointParameters( std::move( route ) ) );
	for ( auto& route : httpapi::embeddingRoutes( *m_db_context ) )
		routes.add( httpapi::declareEndpointParameters( std::move( route ) ) );
	for ( auto& route : httpapi::recordRoutes( *m_db_context ) )
		routes.add( httpapi::declareEndpointParameters( std::move( route ) ) );
	for ( auto& route : httpapi::maintenanceRoutes( *m_db_context ) )
		routes.add( httpapi::declareEndpointParameters( std::move( route ) ) );
	for ( auto& route : httpapi::jobRoutes( *m_db_context ) )
		routes.add( httpapi::declareEndpointParameters( std::move( route ) ) );
	for ( auto& route : httpapi::clusterRoutes( *m_db_context ) )
		routes.add( httpapi::declareEndpointParameters( std::move( route ) ) );
	for ( auto& route : httpapi::downloadSessionRoutes( *m_db_context ) )
		routes.add( httpapi::declareEndpointParameters( std::move( route ) ) );
	for ( auto& route : httpapi::hydrusRoutes( *m_db_context ) )
		routes.add( httpapi::declareEndpointParameters( std::move( route ) ) );
	routes.seal();

	m_http_server = std::make_unique< http::HTTPServer >( http_config, std::move( routes ) );
	m_http_server->start();

	log::info( "IDHAN initialization finished" );
	log::info( "Server available at http://localhost:{}", m_http_server->port() );
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

	log::trace( "Setting up temp path" );
	setupTempPath();
	log::trace( "Temp path setup completed" );

	IOUring::init();
	log::trace( "IOUring engine initialised" );

	m_module_loader = std::make_unique< modules::ModuleLoader >();

	m_clusters = std::make_unique< filesystem::ClusterManager >();
	log::info( "Thumbnails location: {}", getThumbnailsPath().string() );

	if ( getPurgeThumbnailsOnBoot() )
	{
		const auto deleted { filesystem::clearThumbnailCache( getThumbnailsPath() ) };
		if ( deleted )
			log::info( "Purged {} cached thumbnails on boot", *deleted );
		else
			log::warn( "Could not purge the thumbnail cache on boot: {}", deleted.error() );
	}

	startReplacementStack();
}

void ServerContext::run()
{
	log::info( "Starting runtime" );

	// The HTTP server runs independently; this keeps the main thread alive until asked to stop.
	sigset_t signal_set {};
	sigemptyset( &signal_set );
	sigaddset( &signal_set, SIGINT );
	sigaddset( &signal_set, SIGTERM );
	pthread_sigmask( SIG_BLOCK, &signal_set, nullptr );

	int received_signal {};
	sigwait( &signal_set, &received_signal );

	log::info( "Received signal {}, shutting down", received_signal );

	log::info( "Exiting runtime" );
}

ServerContext::~ServerContext()
{
	if ( m_heartbeat != nullptr ) m_heartbeat->shutdown();
	getJobRuntime().shutdownEvents();
	downloader::debugEvents().shutdown();
	if ( m_http_server != nullptr ) m_http_server->stop();
	m_tag_count_maintenance.reset();
	m_hash_maintenance.reset();
	m_module_maintenance.reset();
	downloader::DownloadSessionEventHub::instance().shutdown();
	downloader::RateLimitEventHub::instance().shutdown();

	downloader::downloadSessionManager().shutdown();
	// The same key setupTempPath() created it under. Reading a different one deleted /tmp/idhan and
	// left the configured directory behind, marker file and all, which aborts the next boot.
	const auto upload_path { config::getSilentDefault< std::string >( "server", "temp_path", "/tmp/idhan" ) };
	std::filesystem::remove_all( upload_path );
}

} // namespace idhan
