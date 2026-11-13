#include "http/LanePool.hpp"

#include <folly/ScopeGuard.h>
#include <folly/coro/BlockingWait.h>
#include <folly/executors/thread_factory/NamedThreadFactory.h>
#include <spdlog/spdlog.h>

#include <ada.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <unordered_set>
#include <utility>

#include "imports/BufferedImportSink.hpp"
#include "HostCallback.hpp"
#include "urls/URLUtils.hpp"
#include "cookies/CookieStore.hpp"
#include "http/UserAgent.hpp"
#include "logging/format_ns.hpp"

namespace idhan::downloader
{

struct LanePool::Exchange
{
	std::shared_ptr< NetworkActivity > network {};
	std::shared_ptr< NetworkActivity::Request > progress {};
	HttpMethod method { HttpMethod::GET };
	std::string url {};
	HttpHeaders headers {};
	std::string body {};
	std::vector< std::string > sensitive_query {};
	TransferOptions options {};
	CookieContext cookies {};
	ImportRequest import {};
	std::shared_ptr< std::atomic_bool > cancellation {};
	std::unique_ptr< BufferedImportSink > sink {};
	TransferCallback callback {};
	std::vector< TransferHop > hops {};
	int redirects {};
	std::unordered_map< std::int32_t, StatusRetry > retry_limits {};
	std::unordered_map< std::int32_t, std::uint32_t > retries {};
	std::unordered_set< std::int32_t > backoff_statuses {};
};

LanePool::LanePool( Config config, SettingsResolver resolver, const std::size_t io_threads ) :
  m_config( std::move( config ) ),
  m_resolver( std::move( resolver ) ),
  m_pool( io_threads ),
  m_import_executor(
	  std::max< std::size_t >( 1, m_config.import_threads ),
	  std::make_shared< folly::NamedThreadFactory >( "dl-import-" ) ),
  m_write_executor(
	  std::max< std::size_t >( 1, m_config.import_threads ),
	  std::make_shared< folly::NamedThreadFactory >( "dl-write-" ) )
{
	armSweep();
}

LanePool::~LanePool()
{
	// Destruction must retain borrowed state until pending imports finish.
	folly::coro::blockingWait( shutdown() );
}

std::string LanePool::keyFor( const std::string_view host ) const
{
	const LaneSettings settings { m_resolver ? m_resolver( host ) : LaneSettings {} };

	return settings.group.has_value() ? format_ns::format( "group:{}", *settings.group ) : std::string { host };
}

std::string LanePool::laneKeyForUrl( const std::string_view url ) const
{
	const auto parsed { ada::parse< ada::url_aggregator >( std::string { url } ) };

	if ( !parsed ) return {};

	return keyFor( detail::normalizeHost( std::string { parsed->get_hostname() } ) );
}

void LanePool::reconfigure( const std::string_view host )
{
	if ( m_resolver == nullptr ) return;

	const auto key { keyFor( host ) };
	const std::scoped_lock lock { m_mutex };
	const auto found { m_policies.find( key ) };

	if ( found == m_policies.end() ) return;

	found->second->configure( m_resolver( host ) );
}

LanePolicy* LanePool::policyFor( const std::string& key )
{
	const std::scoped_lock lock { m_mutex };
	const auto found { m_policies.find( key ) };

	return found == m_policies.end() ? nullptr : found->second.get();
}

std::shared_ptr< Lane > LanePool::laneFor( const std::string& key, const std::string_view host )
{
	const std::scoped_lock lock { m_mutex };

	if ( m_stopped ) return {};

	if ( const auto found { m_lanes.find( key ) }; found != m_lanes.end() ) return found->second;

	auto [ policy, created ] { m_policies.try_emplace( key, nullptr ) };

	if ( created )
	{
		LaneSettings settings { m_resolver ? m_resolver( host ) : LaneSettings {} };

		if ( !settings.rate.has_value() && !settings.group.has_value() && m_resolver == nullptr )
			settings.rate = m_config.default_rate;

		policy->second = std::make_unique< LanePolicy >( key, std::move( settings ) );
	}

	auto lane { std::make_shared< Lane >( key, *policy->second, m_pool, m_config.lane, m_config.observer ) };
	m_lanes.emplace( key, lane );
	return std::move( lane );
}

static bool isRedirect( const std::int32_t status )
{
	return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

static bool sameOrigin( const std::string_view left, const std::string_view right )
{
	const auto first { ada::parse< ada::url_aggregator >( left ) };
	const auto second { ada::parse< ada::url_aggregator >( right ) };

	if ( !first || !second ) return false;

	return first->get_protocol() == second->get_protocol() && first->get_host() == second->get_host();
}

void LanePool::send( TransferRequest request, TransferCallback callback )
{
	if ( request.sink )
		request.buffered_sink = std::make_unique< BufferedImportSink >( std::move( request.sink ), m_write_executor );

	auto exchange { std::make_shared< Exchange >() };
	exchange->network = request.network;
	exchange->progress = request.progress;
	exchange->method = request.method;
	exchange->url = std::move( request.url );
	exchange->headers = std::move( request.headers );
	exchange->body = std::move( request.body );
	exchange->sensitive_query = std::move( request.sensitive_query );
	exchange->options = request.options;
	exchange->cookies = request.cookies;
	exchange->import = std::move( request.import );
	exchange->cancellation = std::move( request.cancellation );
	exchange->sink = std::move( request.buffered_sink );
	exchange->callback = std::move( callback );
	dispatch( exchange );
}

static bool isRateLimitHeader( std::string_view name )
{
	std::string lowered { name };
	std::ranges::transform(
		lowered,
		lowered.begin(),
		[]( const unsigned char character ) { return static_cast< char >( std::tolower( character ) ); } );

	if ( lowered == "retry-after" ) return true;

	return lowered.find( "ratelimit" ) != std::string::npos || lowered.find( "rate-limit" ) != std::string::npos;
}

void LanePool::reportRateLimitHeaders( const std::string& key, LanePolicy* policy, const HttpHeaders& headers )
{
	std::string reported {};

	for ( const auto& [ name, value ] : headers )
	{
		if ( !isRateLimitHeader( name ) ) continue;

		if ( !reported.empty() ) reported += ", ";

		reported += format_ns::format( "{}={}", name, value );
	}

	if ( reported.empty() ) return;

	if ( policy != nullptr ) policy->advertised( reported );

	spdlog::trace( "downloader http: lane '{}' rate-limit headers: {}", key, reported );
}

void LanePool::publish( const std::string& key )
{
	if ( m_config.observer == nullptr ) return;

	LaneSnapshot snapshot {};

	{
		const std::scoped_lock lock { m_mutex };
		const auto policy { m_policies.find( key ) };

		if ( policy == m_policies.end() ) return;

		policy->second->fill( snapshot );

		if ( const auto lane { m_lanes.find( key ) }; lane != m_lanes.end() ) lane->second->fill( snapshot );
	}

	(void)hostCallback( [ & ] { m_config.observer->onLaneChanged( snapshot ); } );
}

void LanePool::dispatch( const std::shared_ptr< Exchange >& exchange )
{
	if ( exchange->cancellation && exchange->cancellation->load() )
	{
		if ( exchange->sink ) (void)hostCallback( [ & ] { exchange->sink->abort(); } );

		exchange->callback(
			std::unexpected(
				TransferError { .code = TransferErrorCode::CANCELLED, .message = "Transfer cancelled" } ) );
		return;
	}

	const auto parsed { ada::parse< ada::url_aggregator >( exchange->url ) };

	if ( !parsed || ( parsed->get_protocol() != "http:" && parsed->get_protocol() != "https:" ) )
	{
		if ( exchange->sink ) (void)hostCallback( [ & ] { exchange->sink->abort(); } );

		exchange->callback(
			std::unexpected(
				TransferError { .code = TransferErrorCode::INVALID_URL,
		                        .message = format_ns::format( "Not an HTTP URL: {}", exchange->url ) } ) );
		return;
	}

	const std::string host { detail::normalizeHost( std::string { parsed->get_hostname() } ) };
	const std::string key { keyFor( host ) };
	const auto lane { laneFor( key, host ) };

	if ( !lane )
	{
		if ( exchange->sink ) (void)hostCallback( [ & ] { exchange->sink->abort(); } );

		exchange->callback(
			std::unexpected(
				TransferError { .code = TransferErrorCode::SHUTDOWN, .message = "The downloader is shutting down" } ) );
		return;
	}

	const LaneSettings settings { m_resolver ? m_resolver( host ) : LaneSettings {} };
	exchange->retry_limits = settings.status_retries;
	exchange->backoff_statuses = settings.backoff_statuses;
	TransferOptions& options { exchange->options };
	options.http_version = settings.http_version.value_or( m_config.http_version );
	options.user_agent = m_config.user_agent;
	options.timeout_ms = m_config.timeout_ms;

	if ( options.max_response_bytes == 0 ) options.max_response_bytes = m_config.max_response_bytes;

	TransferRequest hop {};
	hop.network = exchange->network;
	hop.progress = exchange->progress;
	if ( hop.progress )
	{
		hop.progress->phase = NetworkActivity::Phase::WAITING;
		hop.progress->received = 0;
		hop.progress->expected = 0;
	}

	hop.method = exchange->method;
	hop.url = exchange->url;
	hop.headers = exchange->headers;
	hop.body = exchange->body;
	hop.sensitive_query = exchange->sensitive_query;
	hop.options = options;
	hop.cookies = exchange->cookies;
	hop.import = exchange->import;
	hop.cancellation = exchange->cancellation;
	hop.buffered_sink = std::move( exchange->sink );

	if ( exchange->cookies.overlay != nullptr && exchange->cookies.store != nullptr )
	{
		hop.headers.erase( "cookie" );
		applyRequestCookies( hop.url, *exchange->cookies.overlay, *exchange->cookies.store, hop.headers );
	}

	lane->submit(
		std::move( hop ),
		[ this, exchange, key ]( TransferResult result )
		{ dispatchCompletion( exchange, key, std::move( result ) ); } );
}

folly::coro::Task< void > LanePool::finishImport(
	std::shared_ptr< Exchange > exchange,
	std::string key,
	TransferResult result )
{
	auto& response { result.value() };
	auto released { std::move( response.import_finished ) };
	const auto release_slot { folly::makeGuard(
		[ & ]
		{
			if ( released ) released();
		} ) };
	response.import_pending = false;
	if ( exchange->progress ) exchange->progress->beginStoring();

	if ( exchange->cancellation && exchange->cancellation->load() )
	{
		(void)hostCallback( [ & ] { response.sink->abort(); } );
		co_await complete(
			exchange, key, std::unexpected( TransferError { TransferErrorCode::CANCELLED, "Import cancelled" } ) );
		co_return;
	}

	const auto finished_e { co_await hostOperation( response.sink->finish( response.import_metadata ) ) };

	if ( !finished_e.has_value() )
	{
		(void)hostCallback( [ & ] { response.sink->abort(); } );
		co_await complete(
			exchange, key, std::unexpected( TransferError { TransferErrorCode::SINK, finished_e.error() } ) );
		co_return;
	}

	response.import = finished_e.value();
	response.sink.reset();
	co_await complete( exchange, key, std::move( result ) );
}

void LanePool::applyResponseBackoff(
	const Exchange& exchange,
	const std::string& key,
	const TransferResponse& response )
{
	auto* policy { policyFor( key ) };
	if ( policy == nullptr ) return;

	if ( response.status == 429 )
	{
		policy->limited(
			response.headers.contains( "retry-after" ) ?
				std::optional { response.headers.get( "retry-after" ) } :
				std::nullopt );
		publish( key );
		return;
	}

	if ( response.status >= 200 && response.status < 300 )
		policy->succeeded();
	else
		policy->unsuccessful();

	bool paused { exchange.backoff_statuses.contains( response.status ) };
	if ( paused ) policy->failed();

	const auto retry { exchange.retry_limits.find( response.status ) };
	if ( retry != exchange.retry_limits.end() )
	{
		const auto attempted { exchange.retries.find( response.status ) };
		const auto attempts { attempted == exchange.retries.end() ? 0 : attempted->second };
		if ( attempts < retry->second.count && retry->second.wait > std::chrono::seconds::zero() )
		{
			policy->hold( retry->second.wait );
			paused = true;
		}
	}

	if ( paused ) publish( key );
}

void LanePool::dispatchCompletion(
	const std::shared_ptr< Exchange >& exchange,
	const std::string& key,
	TransferResult result )
{
	// Apply the shared pause before LaneShard releases its slot and pumps more work.
	if ( result.has_value() )
		applyResponseBackoff( *exchange, key, result.value() );
	else
	{
		auto* policy { policyFor( key ) };
		if ( policy != nullptr ) policy->unsuccessful();
	}

	if ( result.has_value() && result.value().import_pending )
	{
		m_import_scope.add(
			folly::coro::co_withExecutor(
				&m_import_executor, logHostTaskErrors( finishImport( exchange, key, std::move( result ) ) ) ) );
		return;
	}

	m_import_scope.add(
		folly::coro::co_withExecutor(
			&m_import_executor, logHostTaskErrors( complete( exchange, key, std::move( result ) ) ) ) );
}

folly::coro::Task< void > LanePool::complete(
	std::shared_ptr< Exchange > exchange,
	std::string key,
	TransferResult result )
{
	LanePolicy* policy { policyFor( key ) };

	if ( !result )
	{
		exchange->callback( std::move( result ) );
		co_return;
	}

	TransferResponse& response { *result };
	exchange->sink = std::move( response.sink );

	if ( exchange->cookies.overlay != nullptr && exchange->cookies.store != nullptr )
	{
		auto collected { collectResponseCookies( response.url, response.headers ) };

		for ( Cookie& cookie : collected.persistent ) co_await exchange->cookies.store->set( std::move( cookie ) );
		for ( Cookie& cookie : collected.session ) exchange->cookies.overlay->set( std::move( cookie ) );

		for ( const Cookie& cookie : collected.removed )
		{
			co_await exchange->cookies.store->erase( cookie.name, cookie.domain, cookie.path );
			exchange->cookies.overlay->erase( cookie.name, cookie.domain, cookie.path );
		}
	}

	reportRateLimitHeaders( key, policy, response.headers );

	const bool rate_limited { policy != nullptr && response.status == 429 };

	if ( rate_limited )
	{
		if ( policy->disabled() )
		{
			spdlog::error(
				"downloader http: lane '{}' disabled after HTTP 429 at maximum backoff; user must enable it", key );
			if ( exchange->sink ) (void)hostCallback( [ & ] { exchange->sink->abort(); } );

			exchange->callback(
				std::unexpected(
					TransferError {
						.code = TransferErrorCode::RATE_LIMITED,
						.message =
							"Lane disabled after repeated HTTP 429 responses at maximum backoff; enable the lane to retry" } ) );
			co_return;
		}

		const auto paused_for { std::chrono::ceil< std::chrono::seconds >( std::max(
			LanePolicy::SteadyClock::duration::zero(), policy->nextSlot() - LanePolicy::SteadyClock::now() ) ) };

		publish( key );
		const auto rate { policy->settings().rate.value_or( RequestRate { 0, 0 } ) };
		spdlog::warn(
			"downloader http: lane '{}' rate limited, burst now {} request(s) per {}s, holding every request for {}s and retrying {}",
			key,
			rate.requests,
			rate.seconds,
			paused_for.count(),
			detail::redactUrlQuery( exchange->url, exchange->sensitive_query ) );
		dispatch( exchange );
		co_return;
	}

	const bool backoff { policy != nullptr && exchange->backoff_statuses.contains( response.status ) };

	if ( backoff )
	{
		spdlog::warn(
			"downloader http: lane '{}' stopping after {} answered {}",
			key,
			detail::redactUrlQuery( response.url, exchange->sensitive_query ),
			response.status );
		publish( key );
	}

	const auto retry { exchange->retry_limits.find( response.status ) };
	if ( retry != exchange->retry_limits.end() && exchange->retries[ response.status ] < retry->second.count )
	{
		++exchange->retries[ response.status ];

		const bool hold { policy != nullptr && retry->second.wait > std::chrono::seconds::zero() };

		if ( hold )
		{
			spdlog::warn(
				"downloader http: lane '{}' holding every request for {}s after {} answered {}",
				key,
				retry->second.wait.count(),
				detail::redactUrlQuery( response.url, exchange->sensitive_query ),
				response.status );
			publish( key );
		}

		dispatch( exchange );
		co_return;
	}

	const std::string location { response.headers.get( "location" ) };
	const bool follow { exchange->options.follow_redirects && isRedirect( response.status ) && !location.empty()
		                && exchange->redirects < exchange->options.max_redirects };

	const auto base {
		follow ? ada::parse< ada::url_aggregator >( response.url ) : ada::result< ada::url_aggregator > {}
	};
	auto next { follow ? ada::parse< ada::url_aggregator >( location, base ? &*base : nullptr ) :
		                 ada::result< ada::url_aggregator > {} };

	if ( !follow || !next )
	{
		if ( exchange->sink ) (void)hostCallback( [ & ] { exchange->sink->abort(); } );

		response.hops = std::move( exchange->hops );
		exchange->callback( std::move( result ) );
		co_return;
	}

	exchange->hops.emplace_back( TransferHop { .url = response.url, .status = response.status } );
	++exchange->redirects;

	const std::string target { next->get_href() };

	if ( response.status == 301 || response.status == 302 || response.status == 303 )
	{
		if ( exchange->method != HttpMethod::HEAD ) exchange->method = HttpMethod::GET;

		exchange->body.clear();
		exchange->headers.erase( "content-type" );
		exchange->headers.erase( "content-length" );
	}

	if ( !sameOrigin( response.url, target ) )
	{
		std::vector< std::string > sensitive {};

		for ( const auto& header : exchange->headers )
		{
			if ( isSensitiveHeader( header.first ) ) sensitive.emplace_back( header.first );
		}

		for ( const std::string& name : sensitive ) exchange->headers.erase( name );
	}

	exchange->url = target;
	exchange->retries.clear();
	dispatch( exchange );
}

void LanePool::cancel( const std::shared_ptr< std::atomic_bool >& cancellation )
{
	if ( !cancellation ) return;

	cancellation->store( true );
	std::vector< std::shared_ptr< Lane > > lanes {};

	{
		const std::scoped_lock lock { m_mutex };
		lanes.reserve( m_lanes.size() );

		for ( const auto& [ key, lane ] : m_lanes ) lanes.emplace_back( lane );
	}

	for ( const auto& lane : lanes ) lane->cancel( cancellation );
}

void LanePool::parserFailed( const std::string_view url )
{
	const auto parsed { ada::parse< ada::url_aggregator >( url ) };
	if ( !parsed.has_value() ) return;

	const std::string host { detail::normalizeHost( std::string { parsed->get_hostname() } ) };
	const auto key { keyFor( host ) };
	const auto lane { laneFor( key, host ) };
	if ( !lane ) return;

	auto* policy { policyFor( key ) };
	if ( policy == nullptr ) return;

	const auto wait {
		policy->limited( std::nullopt, LanePolicy::SteadyClock::now(), LanePolicy::SystemClock::now(), true )
	};
	spdlog::warn(
		"downloader http: lane '{}' backing off after parser error, holding requests for {}s",
		key,
		std::chrono::ceil< std::chrono::seconds >( wait ).count() );
	publish( key );
}

void LanePool::resetBackoff( const std::string_view key )
{
	const std::string name { key };
	std::shared_ptr< Lane > lane {};

	{
		const std::scoped_lock lock { m_mutex };
		const auto found { m_policies.find( name ) };

		if ( found == m_policies.end() ) return;

		found->second->reset();

		if ( const auto held { m_lanes.find( name ) }; held != m_lanes.end() ) lane = held->second;
	}

	if ( lane ) lane->wake();

	publish( name );
}

void LanePool::resetAllBackoff()
{
	std::vector< std::string > keys {};
	std::vector< std::shared_ptr< Lane > > lanes {};

	{
		const std::scoped_lock lock { m_mutex };
		keys.reserve( m_policies.size() );

		for ( auto& [ key, policy ] : m_policies )
		{
			policy->reset();
			keys.emplace_back( key );
		}

		lanes.reserve( m_lanes.size() );

		for ( const auto& [ key, lane ] : m_lanes ) lanes.emplace_back( lane );
	}

	for ( const auto& lane : lanes ) lane->wake();

	for ( const std::string& key : keys ) publish( key );
}

std::vector< LaneSnapshot > LanePool::snapshots() const
{
	std::vector< LaneSnapshot > output {};
	const std::scoped_lock lock { m_mutex };
	output.reserve( m_policies.size() );

	for ( const auto& [ key, policy ] : m_policies )
	{
		LaneSnapshot snapshot {};
		policy->fill( snapshot );

		if ( const auto lane { m_lanes.find( key ) }; lane != m_lanes.end() ) lane->second->fill( snapshot );

		output.emplace_back( std::move( snapshot ) );
	}

	return output;
}

void LanePool::armSweep()
{
	{
		const std::scoped_lock lock { m_mutex };

		if ( m_stopped || m_sweep_armed ) return;

		m_sweep_armed = true;
	}

	const auto interval { std::min( m_config.keep_alive / 2, std::chrono::seconds { 1 } ) };
	m_pool.leastLoaded().postAfter( interval, [ this ] { sweep(); } );
}

void LanePool::sweep()
{
	std::vector< std::shared_ptr< Lane > > retired {};
	std::vector< std::string > keys {};

	{
		const std::scoped_lock lock { m_mutex };
		m_sweep_armed = false;

		if ( m_stopped ) return;

		for ( auto lane = m_lanes.begin(); lane != m_lanes.end(); )
		{
			keys.emplace_back( lane->first );

			if ( !lane->second->retirable( m_config.keep_alive ) )
			{
				++lane;
				continue;
			}

			retired.emplace_back( std::move( lane->second ) );
			lane = m_lanes.erase( lane );
		}
	}

	for ( const auto& lane : retired )
	{
		spdlog::debug( "downloader http: retiring lane {}", lane->key() );
		lane->shutdown();
	}

	for ( const auto& key : keys ) publish( key );

	armSweep();
}

folly::coro::Task< void > LanePool::shutdown()
{
	const auto shutdown_lock { co_await m_shutdown_mutex.co_scoped_lock() };
	std::vector< std::shared_ptr< Lane > > lanes {};

	{
		const std::scoped_lock lock { m_mutex };

		if ( m_stopped ) co_return;

		m_stopped = true;
		lanes.reserve( m_lanes.size() );

		for ( auto& [ key, lane ] : m_lanes ) lanes.emplace_back( std::move( lane ) );

		m_lanes.clear();
	}

	for ( const auto& lane : lanes ) lane->shutdown();

	m_pool.stop();
	co_await m_import_scope.joinAsync();
	m_import_executor.join();
	m_write_executor.join();
}

} // namespace idhan::downloader
