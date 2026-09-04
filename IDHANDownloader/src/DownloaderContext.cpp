#include "IDHANDownloader/DownloaderContext.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <mutex>
#include <utility>
#include <vector>

#include "SessionContext.hpp"
#include "cookies/CookieStore.hpp"
#include "http/LanePool.hpp"
#include "js/ScriptRunner.hpp"
#include "logging/format_ns.hpp"
#include "scripts/ScriptRegistry.hpp"

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

	mutable std::mutex mutex {};
	std::vector< std::weak_ptr< SessionContext > > sessions {};
	bool stopped {};

	explicit Impl( DownloaderHost session_host ) : host( session_host ), cookies( session_host.cookies ) {}
};

DownloaderContext::DownloaderContext( std::unique_ptr< Impl > impl ) : m_impl( std::move( impl ) )
{}

DownloaderContext::~DownloaderContext()
{
	shutdown();
}

std::expected< std::unique_ptr< DownloaderContext >, std::string > DownloaderContext::create(
	DownloaderConfig config,
	DownloaderHost host )
{
	if ( config.parser_directory.empty() ) return std::unexpected( "The downloader needs a parser directory" );

	if ( config.url_classes.empty() ) config.url_classes = config.parser_directory / "url-classes.json";

	auto impl { std::make_unique< Impl >( host ) };

	const LaneSettings defaults {
		.rate = config.default_rate,
		.keep_alive = config.lane_keep_alive,
		.error_backoff = config.lane_error_backoff,
		.http_version = config.http_version
	};

	auto registry { ScriptRegistry::create( config.url_classes, config.parser_directory, defaults ) };

	if ( !registry ) return std::unexpected( std::move( registry.error() ) );

	impl->registry = std::move( *registry );
	impl->cookies.load();

	LanePool::Config lanes {
		.lane = Lane::Config { .max_shards = config.max_shards_per_lane,
		                       .shard_growth_threshold = config.shard_growth_threshold,
		                       .unthrottled_concurrency = config.unthrottled_lane_concurrency,
		                       .throttled_concurrency = config.throttled_lane_concurrency },
		.keep_alive = config.lane_keep_alive,
		.default_rate = config.default_rate,
		.http_version = config.http_version,
		.user_agent = config.user_agent,
		.max_response_bytes = config.max_response_bytes,
		.observer = host.lanes
	};

	ScriptRegistry* resolver_registry { impl->registry.get() };
	impl->lanes = std::make_unique< LanePool >(
		std::move( lanes ),
		[ resolver_registry ]( const std::string_view host_name )
		{ return resolver_registry->laneSettings( host_name ); },
		config.io_threads );

	impl->runner = std::make_unique< ScriptRunner >(
		ScriptRunner::Options {
			.threads = config.script_threads,
			.script = ScriptContext::Options { .memory_limit = config.worker_memory_limit,
	                                           .stack_limit = config.worker_stack_limit,
	                                           .burst_timeout = config.script_burst_timeout } },
		impl->registry->bytecode() );

	impl->config = std::move( config );
	return std::unique_ptr< DownloaderContext > { new DownloaderContext { std::move( impl ) } };
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
		.config = m_impl->config
	};

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

void DownloaderContext::shutdown()
{
	std::vector< std::shared_ptr< SessionContext > > sessions {};

	{
		const std::scoped_lock lock { m_impl->mutex };

		if ( m_impl->stopped ) return;

		m_impl->stopped = true;

		for ( const auto& held : m_impl->sessions )
		{
			if ( auto session { held.lock() } ) sessions.emplace_back( std::move( session ) );
		}

		m_impl->sessions.clear();
	}

	for ( const auto& session : sessions ) session->close();
	for ( const auto& session : sessions ) session->wait();

	m_impl->runner->stop();
	sessions.clear();
	m_impl->lanes->shutdown();
}

} // namespace idhan::downloader
