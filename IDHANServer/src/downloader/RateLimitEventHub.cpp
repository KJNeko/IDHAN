#include "RateLimitEventHub.hpp"
#include "DebugEvents.hpp"
#include "events/ServerEvents.hpp"

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

std::vector< http::SSEInitialEvent > RateLimitEventHub::snapshotEvents()
{
	std::vector< StoredSnapshot > snapshots {};
	{
		std::lock_guard lock { m_mutex };
		snapshots.reserve( m_limits.size() );
		for ( const auto& [ key, stored ] : m_limits )
		{
			(void)key;
			snapshots.emplace_back( stored );
		}
	}

	std::ranges::sort(
		snapshots, {}, []( const StoredSnapshot& stored ) -> const std::string& { return stored.snapshot.key; } );

	std::vector< http::SSEInitialEvent > events {};
	events.reserve( snapshots.size() );
	for ( const auto& stored : snapshots )
	{
		auto json { serialize( currentSnapshot( stored.snapshot, stored.captured_at ) ) };
		if ( json ) events.push_back( { "downloader.rate_limit", stored.snapshot.key, std::move( *json ) } );
	}

	return events;
}

void RateLimitEventHub::onLaneChanged( const LaneSnapshot& snapshot )
{
	{
		std::lock_guard lock { m_mutex };
		m_limits.insert_or_assign(
			snapshot.key, StoredSnapshot { .snapshot = snapshot, .captured_at = SteadyClock::now() } );
	}

	auto event { serialize( snapshot ) };
	if ( event ) server::events().publish( "downloader.rate_limit", snapshot.key, std::move( *event ) );
	notifyDebugChanged();
}

} // namespace idhan::downloader
