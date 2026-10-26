#include "http/LanePolicy.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace idhan::downloader;
using Clock = LanePolicy::SteadyClock;

//! The rate tests are about cadence, so they opt out of the error pause laid over it.
static LanePolicy throttled( const std::uint64_t requests, const std::uint64_t seconds )
{
	return LanePolicy {
		"host",
		LaneSettings { .rate = RequestRate { requests, seconds }, .error_backoff = std::chrono::seconds::zero() }
	};
}

static LanePolicy unthrottled( const std::chrono::seconds error_backoff = std::chrono::seconds::zero() )
{
	return LanePolicy { "host", LaneSettings { .error_backoff = error_backoff } };
}

TEST_CASE( "A throttled lane books one slot per interval", "[downloader][lane]" )
{
	auto policy { throttled( 1, 2 ) };
	const Clock::time_point start { std::chrono::seconds { 100 } };

	CHECK_FALSE( policy.claim( start ).has_value() );

	const auto blocked { policy.claim( start ) };
	REQUIRE( blocked.has_value() );
	CHECK( *blocked == std::chrono::seconds { 2 } );

	CHECK_FALSE( policy.claim( start + std::chrono::seconds { 2 } ).has_value() );
}

TEST_CASE( "An unthrottled lane never asks a caller to wait", "[downloader][lane]" )
{
	auto policy { unthrottled() };
	const Clock::time_point start { std::chrono::seconds { 100 } };

	CHECK_FALSE( policy.claim( start ).has_value() );
	CHECK_FALSE( policy.claim( start ).has_value() );
	CHECK_FALSE( policy.claim( start ).has_value() );
	CHECK_FALSE( policy.throttled() );
}

TEST_CASE( "A 429 widens the interval exponentially", "[downloader][lane]" )
{
	auto policy { throttled( 1, 1 ) };
	const Clock::time_point start { std::chrono::seconds { 100 } };

	policy.limited( std::nullopt, start );

	LaneSnapshot first {};
	policy.fill( first, start );
	CHECK( first.effective_interval == std::chrono::seconds { 2 } );
	CHECK( first.consecutive_failures == 1 );
	CHECK( first.backed_off );

	policy.limited( std::nullopt, start + std::chrono::seconds { 2 } );

	LaneSnapshot second {};
	policy.fill( second, start );
	CHECK( second.effective_interval == std::chrono::seconds { 4 } );
	CHECK( second.consecutive_failures == 2 );
}

TEST_CASE( "Retry-After wins when it asks for longer than the widened interval", "[downloader][lane]" )
{
	auto policy { throttled( 1, 1 ) };
	const Clock::time_point start { std::chrono::seconds { 100 } };

	policy.limited( std::string_view { "30" }, start );

	const auto wait { policy.claim( start ) };
	REQUIRE( wait.has_value() );
	CHECK( *wait == std::chrono::seconds { 30 } );
}

TEST_CASE( "Retry-After replaces a longer configured backoff", "[downloader][lane]" )
{
	auto policy { unthrottled( std::chrono::seconds { 30 } ) };
	const Clock::time_point start { std::chrono::seconds { 100 } };

	CHECK( policy.limited( std::string_view { "5" }, start ) == std::chrono::seconds { 5 } );

	const auto wait { policy.claim( start ) };
	REQUIRE( wait.has_value() );
	CHECK( *wait == std::chrono::seconds { 5 } );

	CHECK_FALSE( policy.claim( start + std::chrono::seconds { 5 } ).has_value() );
}

TEST_CASE( "Retry-After never asks a lane to outrun its configured rate", "[downloader][lane]" )
{
	auto policy { throttled( 1, 10 ) };
	const Clock::time_point start { std::chrono::seconds { 100 } };

	CHECK( policy.limited( std::string_view { "1" }, start ) == std::chrono::seconds { 10 } );
	CHECK( policy.claim( start ) == std::chrono::seconds { 10 } );
}

TEST_CASE( "A 429 pauses a lane the moment it arrives", "[downloader][lane]" )
{
	auto policy { unthrottled() };
	const Clock::time_point start { std::chrono::seconds { 100 } };

	REQUIRE_FALSE( policy.claim( start ).has_value() );

	policy.limited( std::string_view { "120" }, start );

	CHECK( policy.nextSlot() == start + std::chrono::seconds { 120 } );
	CHECK( policy.claim( start ) == std::chrono::seconds { 120 } );
	CHECK( policy.claim( start + std::chrono::seconds { 119 } ) == std::chrono::seconds { 1 } );
}

TEST_CASE( "A Retry-After pause still widens the cadence behind it", "[downloader][lane]" )
{
	auto policy { throttled( 1, 1 ) };
	const Clock::time_point start { std::chrono::seconds { 100 } };

	policy.limited( std::string_view { "5" }, start );

	LaneSnapshot snapshot {};
	policy.fill( snapshot, start );
	CHECK( snapshot.effective_interval == std::chrono::seconds { 2 } );
	CHECK( snapshot.consecutive_failures == 1 );
	CHECK( snapshot.remaining == std::chrono::seconds { 5 } );
}

TEST_CASE( "A second 429 inside an open pause cannot shorten it", "[downloader][lane]" )
{
	auto policy { unthrottled() };
	const Clock::time_point start { std::chrono::seconds { 100 } };

	policy.limited( std::string_view { "60" }, start );
	policy.limited( std::string_view { "1" }, start + std::chrono::seconds { 1 } );

	CHECK( policy.nextSlot() == start + std::chrono::seconds { 60 } );
}

TEST_CASE( "Retry-After parses both the delta and the HTTP date form", "[downloader][lane]" )
{
	using SystemClock = LanePolicy::SystemClock;

	CHECK( LanePolicy::parseRetryAfter( "120" ) == std::chrono::seconds { 120 } );
	CHECK_FALSE( LanePolicy::parseRetryAfter( "not-a-date" ).has_value() );

	const auto now { SystemClock::from_time_t( 1'000'000'000 ) };
	const auto parsed { LanePolicy::parseRetryAfter( "Sun, 09 Sep 2001 01:47:00 GMT", now ) };
	REQUIRE( parsed.has_value() );
	CHECK( *parsed == std::chrono::seconds { 20 } );
}

TEST_CASE( "A successful request never clears a lane's backoff", "[downloader][lane]" )
{
	auto policy { throttled( 1, 1 ) };
	const Clock::time_point start { std::chrono::seconds { 100 } };

	policy.limited( std::nullopt, start );

	for ( int index = 0; index < 5; ++index )
	{
		LaneSnapshot snapshot {};
		policy.fill( snapshot, start );
		CHECK( snapshot.effective_interval == std::chrono::seconds { 2 } );
		CHECK( policy.claim( start + std::chrono::seconds { 10 * ( index + 1 ) } ) == std::nullopt );
	}

	LaneSnapshot before {};
	policy.fill( before, start );
	CHECK( before.backed_off );
	CHECK( before.consecutive_failures == 1 );

	policy.reset();

	LaneSnapshot after {};
	policy.fill( after, start );
	CHECK( after.effective_interval == std::chrono::seconds { 1 } );
	CHECK( after.consecutive_failures == 0 );
	CHECK_FALSE( after.backed_off );
}

TEST_CASE( "A non-429 failure backs a lane off too", "[downloader][lane]" )
{
	auto policy { throttled( 1, 1 ) };
	const Clock::time_point start { std::chrono::seconds { 100 } };

	policy.failed( start );

	LaneSnapshot snapshot {};
	policy.fill( snapshot, start );
	CHECK( snapshot.effective_interval == std::chrono::seconds { 2 } );
	CHECK( snapshot.consecutive_failures == 1 );
}

TEST_CASE( "An unthrottled lane still backs off from a one second floor", "[downloader][lane]" )
{
	auto policy { unthrottled() };
	const Clock::time_point start { std::chrono::seconds { 100 } };

	policy.failed( start );

	const auto wait { policy.claim( start ) };
	REQUIRE( wait.has_value() );
	CHECK( *wait == std::chrono::seconds { 1 } );
}

TEST_CASE( "A failed request stops its lane for the whole backoff", "[downloader][lane]" )
{
	auto policy { unthrottled( std::chrono::seconds { 30 } ) };
	const Clock::time_point start { std::chrono::seconds { 100 } };

	REQUIRE_FALSE( policy.claim( start ).has_value() );

	policy.failed( start );

	const auto blocked { policy.claim( start ) };
	REQUIRE( blocked.has_value() );
	CHECK( *blocked == std::chrono::seconds { 30 } );

	CHECK( policy.claim( start + std::chrono::seconds { 29 } ) == std::chrono::seconds { 1 } );
	CHECK_FALSE( policy.claim( start + std::chrono::seconds { 30 } ).has_value() );
}

TEST_CASE( "Failures arriving inside a pause do not compound it", "[downloader][lane]" )
{
	auto policy { unthrottled( std::chrono::seconds { 30 } ) };
	const Clock::time_point start { std::chrono::seconds { 100 } };

	for ( int index = 0; index < 16; ++index ) policy.failed( start );

	LaneSnapshot snapshot {};
	policy.fill( snapshot, start );
	CHECK( snapshot.consecutive_failures == 1 );
	CHECK( snapshot.remaining == std::chrono::seconds { 30 } );
}

TEST_CASE( "A failure after the pause expires widens the next one", "[downloader][lane]" )
{
	auto policy { unthrottled( std::chrono::seconds { 30 } ) };
	Clock::time_point now { std::chrono::seconds { 100 } };

	for ( int index = 0; index < 6; ++index )
	{
		policy.failed( now );
		now = policy.nextSlot();
	}

	LaneSnapshot snapshot {};
	policy.fill( snapshot, now );
	CHECK( snapshot.consecutive_failures == 6 );
	CHECK( snapshot.effective_interval == std::chrono::seconds { 32 } );

	policy.reset();
	CHECK_FALSE( policy.claim( now ).has_value() );
}

TEST_CASE( "Reconfiguring a lane clears the interval derived from the old rate", "[downloader][lane]" )
{
	auto policy { throttled( 1, 1 ) };
	policy.limited( std::nullopt, Clock::time_point { std::chrono::seconds { 100 } } );
	policy.configure( LaneSettings { .rate = RequestRate { 1, 5 } } );

	LaneSnapshot snapshot {};
	policy.fill( snapshot, Clock::time_point { std::chrono::seconds { 100 } } );
	CHECK( snapshot.effective_interval == std::chrono::seconds { 5 } );
	CHECK( snapshot.consecutive_failures == 0 );
	CHECK( snapshot.rate_seconds == 5 );
}

TEST_CASE( "Lane concurrency follows whether the lane is throttled", "[downloader][lane]" )
{
	CHECK( throttled( 1, 1 ).concurrency( 32, 4 ) == 4 );
	CHECK( unthrottled().concurrency( 32, 4 ) == 32 );
	CHECK( LanePolicy( "host", LaneSettings { .concurrency = 7 } ).concurrency( 32, 4 ) == 7 );
}

TEST_CASE( "Claiming a slot late does not shift the ones after it", "[downloader][lane]" )
{
	auto policy { throttled( 1, 1 ) };
	const Clock::time_point start { std::chrono::seconds { 100 } };
	constexpr Clock::duration jitter { std::chrono::milliseconds { 40 } };

	REQUIRE_FALSE( policy.claim( start ).has_value() );

	for ( int index = 1; index <= 10; ++index )
	{
		const auto slot { start + std::chrono::seconds { index } };
		CHECK( policy.nextSlot() == slot );
		REQUIRE_FALSE( policy.claim( slot + jitter ).has_value() );
	}

	CHECK( policy.nextSlot() == start + std::chrono::seconds { 11 } );
}

TEST_CASE( "A lane idle past its interval gets one slot, not a burst", "[downloader][lane]" )
{
	auto policy { throttled( 1, 1 ) };
	const Clock::time_point start { std::chrono::seconds { 100 } };

	REQUIRE_FALSE( policy.claim( start ).has_value() );

	const auto idled { start + std::chrono::seconds { 60 } };
	REQUIRE_FALSE( policy.claim( idled ).has_value() );

	const auto blocked { policy.claim( idled ) };
	REQUIRE( blocked.has_value() );
	CHECK( *blocked == std::chrono::seconds { 1 } );
}

TEST_CASE( "Clearing a lane's backoff moves its generation", "[downloader][lane]" )
{
	auto policy { throttled( 1, 1 ) };
	const std::uint64_t initial { policy.generation() };

	policy.limited( std::nullopt );
	CHECK( policy.generation() == initial );

	policy.reset();
	CHECK( policy.generation() > initial );

	const std::uint64_t cleared { policy.generation() };
	policy.configure( LaneSettings { .rate = RequestRate { 1, 2 } } );
	CHECK( policy.generation() > cleared );
}
