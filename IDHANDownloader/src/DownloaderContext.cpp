#include <folly/coro/BlockingWait.h>
#include <folly/coro/Mutex.h>
#include "IDHANDownloader/DownloaderContext.hpp"

#include <folly/executors/thread_factory/NamedThreadFactory.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <mutex>
#include <map>
#include <utility>
#include <vector>

#include "session/SessionContext.hpp"
#include "cookies/CookieStore.hpp"
#include "http/LanePool.hpp"
#include "js/ScriptRunner.hpp"
#include "logging/format_ns.hpp"
#include "scripts/ScriptRegistry.hpp"
#include "scripts/ScriptWatcher.hpp"

namespace idhan::downloader
{

class DownloaderContext::Impl
{
  public:

	DownloaderConfig config {};
	DownloaderHost host {};
	std::unique_ptr< ScriptRegistry > registry {};
	CookieStore cookies;
	std::unique_ptr< LanePool > lanes {};
	std::unique_ptr< ScriptRunner > runner {};
	std::unique_ptr< folly::CPUThreadPoolExecutor > import_executor {};
	std::unique_ptr< ScriptWatcher > watcher {};

	mutable std::mutex mutex {};
	folly::coro::Mutex shutdown_mutex {};
	std::vector< std::weak_ptr< SessionContext > > sessions {};
	bool stopped {};

	explicit Impl( DownloaderHost session_host ) : host( session_host ), cookies( session_host.cookies ) {}
};

DownloaderContext::DownloaderContext( std::unique_ptr< Impl > impl ) : m_impl( std::move( impl ) )
{}

std::expected< NormalizedUrl, std::string > DownloaderContext::normalizeUrl( const std::string_view url ) const
{
	return m_impl->registry->normalizeUrl( url );
}

DownloaderContext::~DownloaderContext()
{
	// C++ destruction cannot suspend; callers normally await shutdown before destroying the context.
	folly::coro::blockingWait( shutdown() );
}

std::expected< ParserUpdateReport, std::string > DownloaderContext::updateParsers()
{
	return m_impl->registry->updateParsers();
}

std::expected< ParserUpdateReport, std::string > DownloaderContext::updateParser( const std::filesystem::path& path )
{
	return m_impl->registry->updateParser( path );
}

Json::Value DownloaderContext::downloadOptions() const
{
	Json::Value result { Json::objectValue };
	result[ "force_import" ] = m_impl->config.force_download;
	result[ "parsers" ] = Json::Value { Json::arrayValue };
	std::map< std::string, Json::Value > selected;
	for ( const auto& parser : m_impl->registry->parsers() )
	{
		const auto& metadata { parser[ "metadata" ] };
		const auto id { metadata[ "id" ].asString() };
		if ( !selected.contains( id ) || metadata[ "version" ].asUInt64() > selected[ id ][ "version" ].asUInt64() )
			selected[ id ] = metadata;
	}
	for ( auto& [ parser_id, metadata ] : selected )
	{
		const auto id { metadata[ "id" ].asString() };
		for ( auto& option : metadata[ "options" ] )
		{
			const auto name { option[ "name" ].asString() };
			if ( m_impl->config.options[ id ].isMember( name ) )
				option[ "default" ] = m_impl->config.options[ id ][ name ];
		}
		Json::Value exposed { Json::objectValue };
		exposed[ "id" ] = id;
		exposed[ "options" ] = std::move( metadata[ "options" ] );
		result[ "parsers" ].append( std::move( exposed ) );
	}
	return result;
}

std::expected< void, std::string > DownloaderContext::installParser(
	std::string filename,
	std::string source,
	bool replace )
{
	return m_impl->registry->installParser( std::move( filename ), std::move( source ), replace );
}

std::expected< std::string, std::string > DownloaderContext::parserSource( std::string filename ) const
{
	return m_impl->registry->parserSource( std::move( filename ) );
}

Json::Value DownloaderContext::parsers() const
{
	Json::Value result { Json::arrayValue };
	for ( const auto& parser : m_impl->registry->parsers() )
	{
		auto metadata { parser[ "metadata" ] };
		metadata.removeMember( "cookies" );
		metadata[ "filename" ] = parser[ "script" ];
		result.append( std::move( metadata ) );
	}

	return result;
}

std::expected< void, std::string > DownloaderContext::setParserHost( std::string host, std::string parser )
{
	const auto host_copy { host };
	const auto result_e { m_impl->registry->setParserHost( std::move( host ), std::move( parser ) ) };
	if ( result_e.has_value() ) m_impl->lanes->reconfigure( host_copy );

	return result_e;
}

void DownloaderContext::removeParserHost( const std::string_view host )
{
	m_impl->registry->removeParserHost( host );
	m_impl->lanes->reconfigure( host );
}

ParserUpdateReport DownloaderContext::parserStatus() const
{
	return m_impl->registry->parserStatus();
}

folly::coro::Task< std::expected< std::unique_ptr< DownloaderContext >, std::string > > DownloaderContext::create(
	DownloaderConfig config,
	DownloaderHost host )
{
	if ( config.parser_directory.empty() ) co_return std::unexpected( "The downloader needs a parser directory" );

	auto impl { std::make_unique< Impl >( host ) };

	const LaneSettings defaults {
		.rate = config.default_rate,
		.keep_alive = config.lane_keep_alive,
		.error_backoff = config.lane_error_backoff,
		.http_version = config.http_version
	};

	auto registry { ScriptRegistry::create( config.url_classes, config.parser_directory, defaults ) };

	if ( !registry ) co_return std::unexpected( std::move( registry.error() ) );

	impl->registry = std::move( *registry );
	const auto loaded_hosts_e { co_await impl->registry->loadParserHosts( host.parser_hosts ) };
	if ( !loaded_hosts_e.has_value() ) co_return std::unexpected( loaded_hosts_e.error() );

	co_await impl->cookies.load();

	LanePool::Config lanes {
		.lane = Lane::Config { .concurrency = config.lane_concurrency },
		.keep_alive = config.lane_keep_alive,
		.default_rate = config.default_rate,
		.http_version = config.http_version,
		.user_agent = config.user_agent,
		.max_response_bytes = config.max_response_bytes,
		.observer = host.lanes,
		.import_threads = config.import_threads
	};

	ScriptRegistry* resolver_registry { impl->registry.get() };
	impl->lanes = std::make_unique< LanePool >(
		std::move( lanes ),
		[ resolver_registry ]( const std::string_view host_name )
		{ return resolver_registry->laneSettings( host_name ); },
		config.io_threads );

	impl->import_executor = std::make_unique< folly::CPUThreadPoolExecutor >(
		std::max< std::size_t >( 1, config.import_threads ),
		std::make_shared< folly::NamedThreadFactory >( "dl-host-" ) );

	impl->runner = std::make_unique< ScriptRunner >(
		ScriptRunner::Options {
			.parser_instances = config.parser_instances,
			.script = ScriptContext::Options { .memory_limit = config.worker_memory_limit,
	                                           .stack_limit = config.worker_stack_limit,
	                                           .burst_timeout = config.script_burst_timeout } },
		impl->registry->bytecode() );

	if ( config.watch_scripts )
	{
		impl->watcher = std::make_unique< ScriptWatcher >(
			*impl->registry,
			config.parser_directory,
			config.url_classes,
			config.script_watch_interval,
			config.script_watch_debounce );
	}

	impl->config = std::move( config );
	co_return std::unique_ptr< DownloaderContext > { new DownloaderContext { std::move( impl ) } };
}

std::shared_ptr< SessionContext > DownloaderContext::createSession( SessionOptions options )
{
	const std::scoped_lock lock { m_impl->mutex };

	if ( m_impl->stopped ) return {};

	SessionEnvironment environment {
		.registry = m_impl->registry.get(),
		.lanes = m_impl->lanes.get(),
		.cookies = &m_impl->cookies,
		.imports = m_impl->host.imports,
		.secrets = m_impl->host.secrets,
		.runner = m_impl->runner.get(),
		.import_executor = m_impl->import_executor.get(),
		.config = m_impl->config
	};

	for ( const auto& id : options.parser_options.getMemberNames() )
	{
		for ( const auto& name : options.parser_options[ id ].getMemberNames() )
			environment.config.options[ id ][ name ] = options.parser_options[ id ][ name ];
	}

	if ( options.force_import.has_value() ) environment.config.force_download = options.force_import.value();

	std::shared_ptr< SessionContext > session { new SessionContext {
		std::make_unique< SessionContext::Impl >( std::move( environment ), std::move( options ) ) } };
	session->impl().adopt( session );

	std::erase_if( m_impl->sessions, []( const auto& held ) { return held.expired(); } );
	m_impl->sessions.emplace_back( session );
	return std::move( session );
}

std::expected< void, std::string > DownloaderContext::validate( const std::string_view url ) const
{
	auto route { m_impl->registry->route( url ) };

	if ( !route ) return std::unexpected( std::move( route.error() ) );
	if ( !route->has_value() ) return std::unexpected( format_ns::format( "No URL class accepts URL: {}", url ) );

	return {};
}

void DownloaderContext::resetBackoff( const std::string_view lane_key )
{
	m_impl->lanes->resetBackoff( lane_key );
}

void DownloaderContext::resetAllBackoff()
{
	m_impl->lanes->resetAllBackoff();
}

std::vector< LaneSnapshot > DownloaderContext::laneSnapshots() const
{
	return m_impl->lanes->snapshots();
}

ParserCacheSnapshot DownloaderContext::cacheSnapshot() const
{
	return m_impl->runner->cacheSnapshot();
}

std::vector< SessionSnapshot > DownloaderContext::sessionSnapshots( const std::uint64_t since ) const
{
	std::vector< std::shared_ptr< SessionContext > > sessions {};

	{
		const std::scoped_lock lock { m_impl->mutex };
		sessions.reserve( m_impl->sessions.size() );

		for ( const auto& weak : m_impl->sessions )
		{
			if ( auto session { weak.lock() } ) sessions.emplace_back( std::move( session ) );
		}
	}

	std::vector< SessionSnapshot > snapshots {};
	snapshots.reserve( sessions.size() );

	for ( const auto& session : sessions ) snapshots.emplace_back( session->snapshot( since ) );

	return snapshots;
}

folly::coro::Task< void > DownloaderContext::shutdown()
{
	const auto shutdown_lock { co_await m_impl->shutdown_mutex.co_scoped_lock() };
	std::vector< std::shared_ptr< SessionContext > > sessions {};

	{
		const std::scoped_lock lock { m_impl->mutex };

		if ( m_impl->stopped ) co_return;

		m_impl->stopped = true;

		for ( const auto& held : m_impl->sessions )
		{
			if ( auto session { held.lock() } ) sessions.emplace_back( std::move( session ) );
		}

		m_impl->sessions.clear();
	}

	m_impl->watcher.reset();

	for ( const auto& session : sessions ) session->impl().shutdown();

	// Stop parser execution and network callbacks before draining the owned import jobs.
	m_impl->runner->stop();
	co_await m_impl->lanes->shutdown();
	for ( const auto& session : sessions ) co_await session->impl().drain();

	m_impl->import_executor->join();
	sessions.clear();
}

} // namespace idhan::downloader
