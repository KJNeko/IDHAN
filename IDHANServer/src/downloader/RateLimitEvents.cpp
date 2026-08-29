#include "RateLimitEvents.hpp"

#include <json/json.h>

#include <algorithm>
#include <chrono>
#include <vector>

#include "logging/log.hpp"

namespace idhan::downloader
{
namespace
{
using SteadyClock = std::chrono::steady_clock;

Json::UInt64 milliseconds( const SteadyClock::duration value )
{
	if ( value <= SteadyClock::duration::zero() ) return 0;

	return static_cast< Json::UInt64 >( std::chrono::ceil< std::chrono::milliseconds >( value ).count() );
}

Json::Value serialize( const LaneSnapshot& snapshot )
{
	Json::Value json {};
	json[ "scheduling_key" ] = snapshot.key;
	json[ "group" ] = snapshot.group.value_or( "" );
	json[ "requests" ] = Json::UInt64 { snapshot.rate_requests };
	json[ "seconds" ] = Json::UInt64 { snapshot.rate_seconds };
	json[ "throttled" ] = snapshot.throttled;
	json[ "effective_interval_ms" ] = milliseconds( snapshot.effective_interval );
	json[ "remaining_ms" ] = milliseconds( snapshot.remaining );
	json[ "consecutive_limits" ] = snapshot.consecutive_failures;
	json[ "backed_off" ] = snapshot.backed_off;
	json[ "in_flight" ] = Json::UInt64 { snapshot.in_flight };
	json[ "queued" ] = Json::UInt64 { snapshot.queued };
	json[ "shards" ] = Json::UInt64 { snapshot.shards };
	json[ "bytes_per_second" ] = Json::UInt64 { snapshot.bytes_per_second };
	json[ "active" ] = snapshot.active;
	return json;
}

LaneSnapshot currentSnapshot( LaneSnapshot snapshot, const SteadyClock::time_point captured_at )
{
	const auto elapsed { std::max( SteadyClock::duration::zero(), SteadyClock::now() - captured_at ) };
	snapshot.remaining = std::max( SteadyClock::duration::zero(), snapshot.remaining - elapsed );
	return snapshot;
}
} // namespace

RateLimitEventHub& RateLimitEventHub::instance()
{
	static RateLimitEventHub hub {};
	return hub;
}

void RateLimitEventHub::subscribe( const drogon::WebSocketConnectionPtr& connection )
{
	std::vector< LaneSnapshot > limits {};
	{
		std::lock_guard lock { m_mutex };
		m_subscribers[ connection.get() ] = connection;
		limits.reserve( m_limits.size() );
		for ( const auto& [ key, stored ] : m_limits )
		{
			(void)key;
			limits.emplace_back( currentSnapshot( stored.snapshot, stored.captured_at ) );
		}
	}

	std::ranges::sort( limits, {}, &LaneSnapshot::key );
	Json::Value message {};
	message[ "type" ] = "rate_limits";
	message[ "limits" ] = Json::arrayValue;
	for ( const auto& snapshot : limits ) message[ "limits" ].append( serialize( snapshot ) );
	if ( connection->connected() ) connection->sendJson( message );
}

void RateLimitEventHub::unsubscribe( const drogon::WebSocketConnectionPtr& connection )
{
	std::lock_guard lock { m_mutex };
	m_subscribers.erase( connection.get() );
}

void RateLimitEventHub::onLaneChanged( const LaneSnapshot& snapshot )
{
	{
		std::lock_guard lock { m_mutex };
		m_limits.insert_or_assign(
			snapshot.key, StoredSnapshot { .snapshot = snapshot, .captured_at = SteadyClock::now() } );
	}

	drogon::app().getLoop()->queueInLoop( [ this, copy = snapshot ] { broadcast( copy ); } );
}

void RateLimitEventHub::broadcast( const LaneSnapshot& snapshot )
{
	std::vector< drogon::WebSocketConnectionPtr > subscribers {};
	{
		std::lock_guard lock { m_mutex };
		for ( auto it { m_subscribers.begin() }; it != m_subscribers.end(); )
		{
			const auto connection { it->second.lock() };
			if ( !connection || !connection->connected() )
			{
				it = m_subscribers.erase( it );
				continue;
			}
			subscribers.emplace_back( connection );
			++it;
		}
	}

	Json::Value message {};
	message[ "type" ] = "rate_limit";
	message[ "limit" ] = serialize( snapshot );
	for ( const auto& connection : subscribers ) connection->sendJson( message );
}

void RateLimitEvents::handleNewConnection(
	[[maybe_unused]] const drogon::HttpRequestPtr& request,
	const drogon::WebSocketConnectionPtr& connection )
{
	log::debug( "Rate limit events: client connected" );
	RateLimitEventHub::instance().subscribe( connection );
}

void RateLimitEvents::handleNewMessage(
	[[maybe_unused]] const drogon::WebSocketConnectionPtr& connection,
	[[maybe_unused]] std::string&& message,
	[[maybe_unused]] const drogon::WebSocketMessageType& type )
{}

void RateLimitEvents::handleConnectionClosed( const drogon::WebSocketConnectionPtr& connection )
{
	log::debug( "Rate limit events: client disconnected" );
	RateLimitEventHub::instance().unsubscribe( connection );
}

} // namespace idhan::downloader
