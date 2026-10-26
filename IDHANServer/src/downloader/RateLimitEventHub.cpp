#include "RateLimitEventHub.hpp"
#include "DebugEvents.hpp"

#include <algorithm>
#include <chrono>
#include <simdjson.h>

#include "logging/log.hpp"

namespace idhan::downloader
{
using SteadyClock = std::chrono::steady_clock;

static std::uint64_t milliseconds( const SteadyClock::duration value )
{
	if ( value <= SteadyClock::duration::zero() ) return 0;

	return static_cast< std::uint64_t >( std::chrono::ceil< std::chrono::milliseconds >( value ).count() );
}

static std::optional< http::ValidatedJson > serialize( const LaneSnapshot& snapshot )
{
	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.append_key_value( "scheduling_key", snapshot.key );
	builder.append_comma();
	builder.append_key_value( "group", snapshot.group.value_or( "" ) );
	builder.append_comma();
	builder.append_key_value( "requests", snapshot.rate_requests );
	builder.append_comma();
	builder.append_key_value( "seconds", snapshot.rate_seconds );
	builder.append_comma();
	builder.append_key_value( "throttled", snapshot.throttled );
	builder.append_comma();
	builder.append_key_value( "effective_interval_ms", milliseconds( snapshot.effective_interval ) );
	builder.append_comma();
	builder.append_key_value( "remaining_ms", milliseconds( snapshot.remaining ) );
	builder.append_comma();
	builder.append_key_value( "consecutive_limits", snapshot.consecutive_failures );
	builder.append_comma();
	builder.append_key_value( "backed_off", snapshot.backed_off );
	builder.append_comma();
	builder.append_key_value( "in_flight", static_cast< std::uint64_t >( snapshot.in_flight ) );
	builder.append_comma();
	builder.append_key_value( "queued", static_cast< std::uint64_t >( snapshot.queued ) );
	builder.append_comma();
	builder.append_key_value( "shards", static_cast< std::uint64_t >( snapshot.shards ) );
	builder.append_comma();
	builder.append_key_value( "download_bytes_per_second", snapshot.download_bytes_per_second );
	builder.append_comma();
	builder.append_key_value( "bytes_per_second", static_cast< std::uint64_t >( snapshot.bytes_per_second ) );
	builder.append_comma();
	builder.append_key_value( "active", snapshot.active );
	builder.end_object();

	auto json { builder.view() };
	if ( json.error() )
	{
		log::error( "Unable to serialize rate-limit event: {}", simdjson::error_message( json.error() ) );
		return std::nullopt;
	}

	auto validated { http::ValidatedJson::parse( std::string { *json } ) };
	if ( !validated )
	{
		log::error( "Unable to validate rate-limit event: {}", validated.error().message );
		return std::nullopt;
	}

	return std::move( *validated );
}

static std::optional< http::ValidatedJson > serializeSnapshot( const std::vector< LaneSnapshot >& limits )
{
	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.escape_and_append_with_quotes( "limits" );
	builder.append_colon();
	builder.start_array();

	bool first { true };
	for ( const auto& snapshot : limits )
	{
		auto event { serialize( snapshot ) };
		if ( !event ) return std::nullopt;

		if ( !first ) builder.append_comma();
		first = false;
		builder.append_raw( event->text() );
	}

	builder.end_array();
	builder.end_object();

	auto json { builder.view() };
	if ( json.error() )
	{
		log::error( "Unable to serialize rate-limit snapshot: {}", simdjson::error_message( json.error() ) );
		return std::nullopt;
	}

	auto validated { http::ValidatedJson::parse( std::string { *json } ) };
	if ( !validated )
	{
		log::error( "Unable to validate rate-limit snapshot: {}", validated.error().message );
		return std::nullopt;
	}

	return std::move( *validated );
}

static LaneSnapshot currentSnapshot( LaneSnapshot snapshot, const SteadyClock::time_point captured_at )
{
	const auto elapsed { std::max( SteadyClock::duration::zero(), SteadyClock::now() - captured_at ) };
	snapshot.remaining = std::max( SteadyClock::duration::zero(), snapshot.remaining - elapsed );

	if ( elapsed >= std::chrono::seconds { 1 } ) snapshot.download_bytes_per_second = 0;

	return snapshot;
}

RateLimitEventHub& RateLimitEventHub::instance()
{
	static RateLimitEventHub hub {};
	return hub;
}

http::SSESubscription RateLimitEventHub::subscribe()
{
	std::vector< LaneSnapshot > limits {};
	std::lock_guard lock { m_mutex };
	limits.reserve( m_limits.size() );
	for ( const auto& [ key, stored ] : m_limits )
	{
		(void)key;
		limits.emplace_back( currentSnapshot( stored.snapshot, stored.captured_at ) );
	}

	std::ranges::sort( limits, {}, &LaneSnapshot::key );
	auto snapshot { serializeSnapshot( limits ) };
	if ( !snapshot ) return m_events.subscribe( {}, {}, false );

	return m_events.subscribe( {}, { std::move( *snapshot ) }, false );
}

void RateLimitEventHub::onLaneChanged( const LaneSnapshot& snapshot )
{
	{
		std::lock_guard lock { m_mutex };
		m_limits.insert_or_assign(
			snapshot.key, StoredSnapshot { .snapshot = snapshot, .captured_at = SteadyClock::now() } );
	}

	auto event { serialize( snapshot ) };
	if ( event ) m_events.publish( snapshot.key, std::move( *event ) );
	notifyDebugChanged();
}

void RateLimitEventHub::shutdown()
{
	m_events.shutdown();
}

} // namespace idhan::downloader
