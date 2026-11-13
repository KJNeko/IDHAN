#include "http/LanePolicy.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <barrier>
#include <thread>
#include <vector>

using namespace idhan::downloader;
using Clock = LanePolicy::SteadyClock;

//! The rate tests are about the window, so they opt out of the error pause laid over it.
static LanePolicy rateLimited( const std::uint64_t requests, const std::uint64_t seconds )
{
	return LanePolicy {
		"host",
		LaneSettings { .rate = RequestRate { requests, seconds }, .error_backoff = std::chrono::seconds::zero() }
	};
}

static LanePolicy unlimited( const std::chrono::seconds error_backoff = std::chrono::seconds::zero() )
{
	return LanePolicy { "host", LaneSettings { .error_backoff = error_backoff } };
}

TEST_CASE( "A one request lane reopens one window later", "[downloader][lane]" )
{
	auto policy { rateLimited( 1, 2 ) };
	const Clock::time_point start { std::chrono::seconds { 100 } };

	CHECK_FALSE( policy.claim( start ).has_value() );

	const auto blocked { policy.claim( start ) };
	REQUIRE( blocked.has_value() );
	CHECK( *blocked == std::chrono::seconds { 2 } );

	CHECK_FALSE( policy.claim( start + std::chrono::seconds { 2 } ).has_value() );
}

TEST_CASE( "Simultaneous callers cannot exceed a shared request window", "[downloader][lane][global-rate]" )
{
	constexpr std::size_t callers { 32 };
	constexpr std::uint64_t allowance { 10 };
	auto policy { rateLimited( allowance, 1 ) };
	std::barrier gate { static_cast< std::ptrdiff_t >( callers ) };
	std::atomic_size_t admitted {};
	const Clock::time_point start { std::chrono::seconds { 100 } };
	std::vector< std::jthread > threads;
	for ( std::size_t index {}; index < callers; ++index )
	{
		threads.emplace_back(
			[ & ]
			{
				gate.arrive_and_wait();
				if ( !policy.claim( start ).has_value() ) ++admitted;
			} );
	}
	threads.clear();
	CHECK( admitted.load() == allowance );
	CHECK( policy.claim( start ) == std::chrono::seconds { 1 } );
	CHECK( policy.claim( start + std::chrono::milliseconds { 999 } ) == std::chrono::milliseconds { 1 } );
}

TEST_CASE( "A lane without a request rate never asks a caller to wait", "[downloader][lane]" )
{
	auto policy { unlimited() };
	const Clock::time_point start { std::chrono::seconds { 100 } };

	CHECK_FALSE( policy.claim( start ).has_value() );
	CHECK_FALSE( policy.claim( start ).has_value() );
	CHECK_FALSE( policy.claim( start ).has_value() );
}

TEST_CASE( "Refreshing host settings preserves its shared rate budget and backoff", "[downloader][lane][global-rate]" )
{
	const Clock::time_point start { std::chrono::seconds { 100 } };
	const LaneSettings settings { .rate = RequestRate { 1, 5 } };
	LanePolicy policy { "host", settings };
	REQUIRE_FALSE( policy.claim( start ).has_value() );
	policy.configure( settings );
	CHECK( policy.claim( start ) == std::chrono::seconds { 5 } );
	policy.limited( std::string_view { "60" }, start );
	policy.configure( settings );
	CHECK( policy.claim( start ) == std::chrono::seconds { 60 } );
}

TEST_CASE( "A 429 without Retry-After starts at one second independently of error backoff", "[downloader][lane]" )
{
	auto policy { unlimited( std::chrono::seconds { 30 } ) };
	const Clock::time_point start { std::chrono::seconds { 100 } };

	CHECK( policy.limited( std::nullopt, start ) == std::chrono::seconds { 1 } );
	CHECK( policy.claim( start ) == std::chrono::seconds { 1 } );
	CHECK_FALSE( policy.claim( start + std::chrono::seconds { 1 } ).has_value() );
}

TEST_CASE(
	"Changing a rate budget preserves the newest claims after its ring wraps",
	"[downloader][lane][global-rate]" )
{
	auto policy { rateLimited( 3, 5 ) };
	const Clock::time_point start { std::chrono::seconds { 100 } };
	for ( const int offset : { 0, 1, 2, 5 } )
		REQUIRE_FALSE( policy.claim( start + std::chrono::seconds { offset } ).has_value() );

	SECTION( "A smaller budget retains the newest starts" )
	{
		policy.configure( LaneSettings { .rate = RequestRate { 2, 5 } } );
		CHECK( policy.claim( start + std::chrono::seconds { 5 } ) == std::chrono::seconds { 2 } );
		REQUIRE_FALSE( policy.claim( start + std::chrono::seconds { 7 } ).has_value() );
		CHECK( policy.claim( start + std::chrono::seconds { 7 } ) == std::chrono::seconds { 3 } );
	}
	SECTION( "A larger budget only adds its additional allowance" )
	{
		policy.configure( LaneSettings { .rate = RequestRate { 4, 5 } } );
		REQUIRE_FALSE( policy.claim( start + std::chrono::seconds { 5 } ).has_value() );
		CHECK( policy.claim( start + std::chrono::seconds { 5 } ) == std::chrono::seconds { 1 } );
	}
}

TEST_CASE( "A 429 pauses for at least a second", "[downloader][lane]" )
{
	const Clock::time_point start { std::chrono::seconds { 100 } };
	auto without_header { unlimited() };
	auto zero_header { unlimited() };

	CHECK( without_header.limited( std::nullopt, start ) == std::chrono::seconds { 1 } );
	CHECK( zero_header.limited( std::string_view { "0" }, start ) == std::chrono::seconds { 1 } );
}

TEST_CASE( "Retry-After holds a lane past its window", "[downloader][lane]" )
{
	auto policy { rateLimited( 1, 1 ) };
	const Clock::time_point start { std::chrono::seconds { 100 } };

	policy.limited( std::string_view { "30" }, start );

	const auto wait { policy.claim( start ) };
	REQUIRE( wait.has_value() );
	CHECK( *wait == std::chrono::seconds { 30 } );
}

TEST_CASE( "Retry-After replaces a longer configured backoff", "[downloader][lane]" )
{
	auto policy { unlimited( std::chrono::seconds { 30 } ) };
	const Clock::time_point start { std::chrono::seconds { 100 } };

	CHECK( policy.limited( std::string_view { "5" }, start ) == std::chrono::seconds { 5 } );

	const auto wait { policy.claim( start ) };
	REQUIRE( wait.has_value() );
	CHECK( *wait == std::chrono::seconds { 5 } );

	CHECK_FALSE( policy.claim( start + std::chrono::seconds { 5 } ).has_value() );
}

TEST_CASE( "A short Retry-After leaves a full window in force", "[downloader][lane]" )
{
	auto policy { rateLimited( 1, 10 ) };
	const Clock::time_point start { std::chrono::seconds { 100 } };

	REQUIRE_FALSE( policy.claim( start ).has_value() );

	CHECK( policy.limited( std::string_view { "1" }, start ) == std::chrono::seconds { 1 } );
	CHECK( policy.claim( start ) == std::chrono::seconds { 10 } );
}

TEST_CASE( "A 429 pauses a lane the moment it arrives", "[downloader][lane]" )
{
	auto policy { unlimited() };
	const Clock::time_point start { std::chrono::seconds { 100 } };

	REQUIRE_FALSE( policy.claim( start ).has_value() );

	policy.limited( std::string_view { "120" }, start );

	CHECK( policy.nextSlot() == start + std::chrono::seconds { 120 } );
	CHECK( policy.claim( start ) == std::chrono::seconds { 120 } );
	CHECK( policy.claim( start + std::chrono::seconds { 119 } ) == std::chrono::seconds { 1 } );
}

TEST_CASE( "Each 429 lowers the burst to a floor of one request per second", "[downloader][lane]" )
{
	auto policy { rateLimited( 8, 1 ) };
	Clock::time_point now { std::chrono::seconds { 100 } };

	for ( std::uint64_t index = 0; index < 10; ++index )
	{
		const auto burst { index < 7 ? 7 - index : 1 };
		policy.limited( std::string_view { "5" }, now );

		LaneSnapshot paused {};
		policy.fill( paused, now );
		CHECK( paused.backed_off );
		CHECK( paused.rate_limited );
		CHECK( paused.rate_limit_hits == index + 1 );
		CHECK( paused.remaining == std::chrono::seconds { 5 } );
		CHECK( paused.rate == ( RequestRate { burst, 1 } ) );
		CHECK( policy.claim( now ) == std::chrono::seconds { 5 } );

		now += std::chrono::seconds { 5 };

		LaneSnapshot after {};
		policy.fill( after, now );
		CHECK_FALSE( after.backed_off );
		CHECK_FALSE( after.rate_limited );
		CHECK( after.rate_limit_hits == index + 1 );
		CHECK( after.rate == ( RequestRate { burst, 1 } ) );

		for ( std::uint64_t request {}; request < burst; ++request ) REQUIRE_FALSE( policy.claim( now ).has_value() );

		CHECK( policy.claim( now ) == std::chrono::seconds { 1 } );
	}
}

TEST_CASE( "429 burst reductions preserve recent claims and survive refreshes", "[downloader][lane]" )
{
	auto policy { rateLimited( 3, 5 ) };
	const Clock::time_point start { std::chrono::seconds { 100 } };
	for ( const int offset : { 0, 1, 2, 5 } )
		REQUIRE_FALSE( policy.claim( start + std::chrono::seconds { offset } ).has_value() );

	policy.limited( std::string_view { "1" }, start + std::chrono::seconds { 5 } );
	CHECK( policy.claim( start + std::chrono::seconds { 6 } ) == std::chrono::seconds { 1 } );
	policy.configure( LaneSettings { .rate = RequestRate { 3, 5 } } );
	CHECK( policy.settings().rate == ( RequestRate { 2, 5 } ) );
	CHECK( policy.claim( start + std::chrono::seconds { 6 } ) == std::chrono::seconds { 1 } );
	REQUIRE_FALSE( policy.claim( start + std::chrono::seconds { 7 } ).has_value() );
	CHECK( policy.claim( start + std::chrono::seconds { 7 } ) == std::chrono::seconds { 3 } );
}

TEST_CASE( "Every 429 during a shared pause reduces the burst", "[downloader][lane]" )
{
	auto policy { rateLimited( 3, 1 ) };
	const Clock::time_point start { std::chrono::seconds { 100 } };
	policy.limited( std::string_view { "60" }, start );
	policy.limited( std::string_view { "1" }, start );
	CHECK( policy.settings().rate == ( RequestRate { 1, 1 } ) );
	CHECK( policy.claim( start ) == std::chrono::seconds { 60 } );
}

TEST_CASE( "An unlimited lane falls back to one request per second after a 429", "[downloader][lane]" )
{
	auto policy { unlimited() };
	const Clock::time_point start { std::chrono::seconds { 100 } };
	policy.limited( std::nullopt, start );
	CHECK( policy.settings().rate == ( RequestRate { 1, 1 } ) );
	REQUIRE_FALSE( policy.claim( start + std::chrono::seconds { 1 } ).has_value() );
	CHECK( policy.claim( start + std::chrono::seconds { 1 } ) == std::chrono::seconds { 1 } );
}

TEST_CASE( "Lane snapshots distinguish 429 cooldowns from other waits", "[downloader][lane]" )
{
	auto policy { rateLimited( 3, 1 ) };
	const Clock::time_point start { std::chrono::seconds { 100 } };
	policy.advertised( "retry-after=5" );
	policy.limited( std::string_view { "5" }, start );
	policy.reset();
	policy.hold( std::chrono::seconds { 10 }, start );
	LaneSnapshot snapshot {};
	policy.fill( snapshot, start );
	CHECK( snapshot.backed_off );
	CHECK_FALSE( snapshot.rate_limited );
	CHECK( snapshot.rate_limit_hits == 1 );
	CHECK( snapshot.advertised_limits == "retry-after=5" );
}

TEST_CASE( "Repeated parser errors only back off without changing rate settings", "[downloader][lane]" )
{
	LaneSettings settings { .error_backoff = std::chrono::seconds { 5 } };
	SECTION( "Burst" )
	{
		settings.rate = RequestRate { 8, 1 };
	}
	SECTION( "Spread" )
	{
		settings.rate = RequestRate { 8, 1, true };
	}
	SECTION( "Unlimited" )
	{}
	LanePolicy policy { "host", settings };
	const Clock::time_point start { std::chrono::seconds { 100 } };
	for ( std::uint64_t attempt {}; attempt < 3; ++attempt )
	{
		policy.limited( std::nullopt, start, LanePolicy::SystemClock::now(), true );
		CHECK( policy.settings().rate == settings.rate );
		CHECK( policy.claim( start ) == std::chrono::seconds { 5 } );
		LaneSnapshot snapshot {};
		policy.fill( snapshot, start );
		CHECK( snapshot.parser_limited );
		CHECK_FALSE( snapshot.rate_limited );
		CHECK( snapshot.parser_error_hits == attempt + 1 );
		CHECK( snapshot.rate_limit_hits == 0 );
	}

	policy.configure( settings );
	CHECK( policy.settings().rate == settings.rate );
}

TEST_CASE( "A second 429 inside an open pause cannot shorten it", "[downloader][lane]" )
{
	auto policy { unlimited() };
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

TEST_CASE( "Clearing a pause lets the lane dispatch at once", "[downloader][lane]" )
{
	auto policy { rateLimited( 1, 1 ) };
	const Clock::time_point start { std::chrono::seconds { 100 } };

	policy.limited( std::string_view { "60" }, start );
	REQUIRE( policy.claim( start ).has_value() );

	policy.reset();

	LaneSnapshot after {};
	policy.fill( after, start );
	CHECK_FALSE( after.backed_off );
	CHECK_FALSE( policy.claim( start ).has_value() );
}

TEST_CASE( "A non-429 failure pauses a lane too", "[downloader][lane]" )
{
	auto policy { LanePolicy {
		"host", LaneSettings { .rate = RequestRate { 2, 1 }, .error_backoff = std::chrono::seconds { 3 } } } };
	const Clock::time_point start { std::chrono::seconds { 100 } };

	policy.failed( start );

	CHECK( policy.claim( start ) == std::chrono::seconds { 3 } );

	const auto resumed { start + std::chrono::seconds { 3 } };
	REQUIRE_FALSE( policy.claim( resumed ).has_value() );
	REQUIRE_FALSE( policy.claim( resumed ).has_value() );
	CHECK( policy.claim( resumed ) == std::chrono::seconds { 1 } );
}

TEST_CASE( "A non-429 failure with no configured backoff does not pause", "[downloader][lane]" )
{
	auto policy { unlimited() };
	const Clock::time_point start { std::chrono::seconds { 100 } };

	policy.failed( start );

	CHECK_FALSE( policy.claim( start ).has_value() );
}

TEST_CASE( "A failed request stops its lane for the whole backoff", "[downloader][lane]" )
{
	auto policy { unlimited( std::chrono::seconds { 30 } ) };
	const Clock::time_point start { std::chrono::seconds { 100 } };

	REQUIRE_FALSE( policy.claim( start ).has_value() );

	policy.failed( start );

	const auto blocked { policy.claim( start ) };
	REQUIRE( blocked.has_value() );
	CHECK( *blocked == std::chrono::seconds { 30 } );

	CHECK( policy.claim( start + std::chrono::seconds { 29 } ) == std::chrono::seconds { 1 } );
	CHECK_FALSE( policy.claim( start + std::chrono::seconds { 30 } ).has_value() );
}

TEST_CASE( "A hold pauses the lane without changing its rate", "[downloader][lane]" )
{
	auto policy { rateLimited( 2, 1 ) };
	const Clock::time_point start { std::chrono::seconds { 100 } };

	policy.hold( std::chrono::seconds { 30 }, start );

	CHECK( policy.claim( start ) == std::chrono::seconds { 30 } );

	const auto resumed { start + std::chrono::seconds { 30 } };
	REQUIRE_FALSE( policy.claim( resumed ).has_value() );
	REQUIRE_FALSE( policy.claim( resumed ).has_value() );
	CHECK( policy.claim( resumed ) == std::chrono::seconds { 1 } );
}

TEST_CASE( "A hold joins a pause that is already open", "[downloader][lane]" )
{
	auto policy { unlimited() };
	const Clock::time_point start { std::chrono::seconds { 100 } };

	policy.hold( std::chrono::seconds { 30 }, start );
	policy.hold( std::chrono::seconds { 30 }, start + std::chrono::seconds { 10 } );

	CHECK( policy.nextSlot() == start + std::chrono::seconds { 30 } );
}

TEST_CASE( "Failures arriving inside a pause do not compound it", "[downloader][lane]" )
{
	auto policy { unlimited( std::chrono::seconds { 30 } ) };
	const Clock::time_point start { std::chrono::seconds { 100 } };

	for ( int index = 0; index < 16; ++index ) policy.failed( start );

	LaneSnapshot snapshot {};
	policy.fill( snapshot, start );
	CHECK( snapshot.remaining == std::chrono::seconds { 30 } );
}

TEST_CASE( "A failure after a pause expires opens one of the same length", "[downloader][lane]" )
{
	auto policy { unlimited( std::chrono::seconds { 30 } ) };
	Clock::time_point now { std::chrono::seconds { 100 } };

	for ( int index = 0; index < 6; ++index )
	{
		policy.failed( now );
		CHECK( policy.nextSlot() == now + std::chrono::seconds { 30 } );
		now = policy.nextSlot();
	}

	policy.reset();
	CHECK_FALSE( policy.claim( now ).has_value() );
}

TEST_CASE( "Reconfiguring a lane applies the new rate and preserves its pause", "[downloader][lane]" )
{
	const Clock::time_point start { std::chrono::seconds { 100 } };
	auto policy { rateLimited( 1, 1 ) };
	policy.limited( std::string_view { "60" }, start );
	policy.configure( LaneSettings { .rate = RequestRate { 1, 5 } } );

	LaneSnapshot snapshot {};
	policy.fill( snapshot, start );
	CHECK( snapshot.backed_off );
	CHECK( snapshot.rate.seconds == 5 );

	CHECK( policy.claim( start ) == std::chrono::seconds { 60 } );
	REQUIRE_FALSE( policy.claim( start + std::chrono::seconds { 60 } ).has_value() );
	CHECK( policy.claim( start + std::chrono::seconds { 60 } ) == std::chrono::seconds { 5 } );
}

TEST_CASE( "Lane concurrency uses one default and preserves host overrides", "[downloader][lane]" )
{
	CHECK( rateLimited( 1, 1 ).concurrency( 32 ) == 1 );
	CHECK( unlimited().concurrency( 32 ) == 32 );
	CHECK( LanePolicy( "host", LaneSettings { .rate = RequestRate { 8, 1, false, 4 } } ).concurrency( 32 ) == 4 );
	CHECK(
		LanePolicy( "host", LaneSettings { .rate = RequestRate { 8, 1, false, 4 }, .concurrency = 2 } )
			.concurrency( 32 )
	    == 2 );
	CHECK( LanePolicy( "host", LaneSettings { .concurrency = 7 } ).concurrency( 32 ) == 7 );
}

TEST_CASE( "A lane releases its whole allowance at once", "[downloader][lane]" )
{
	auto policy { rateLimited( 10, 1 ) };
	const Clock::time_point start { std::chrono::seconds { 100 } };

	for ( int index = 0; index < 10; ++index ) REQUIRE_FALSE( policy.claim( start ).has_value() );

	CHECK( policy.claim( start ) == std::chrono::seconds { 1 } );
	CHECK( policy.nextSlot() == start + std::chrono::seconds { 1 } );

	for ( int index = 0; index < 10; ++index )
		REQUIRE_FALSE( policy.claim( start + std::chrono::seconds { 1 } ).has_value() );

	CHECK( policy.claim( start + std::chrono::seconds { 1 } ) == std::chrono::seconds { 1 } );
}

TEST_CASE( "Spread rates evenly space starts without accumulating idle credit", "[downloader][lane]" )
{
	LanePolicy policy { "host", LaneSettings { .rate = RequestRate { 8, 1, true } } };
	const Clock::time_point start { std::chrono::seconds { 100 } };
	for ( int index {}; index < 16; ++index )
	{
		const auto now { start + std::chrono::milliseconds { index * 125 } };
		REQUIRE_FALSE( policy.claim( now ).has_value() );
		CHECK( policy.claim( now ) == std::chrono::milliseconds { 125 } );
		CHECK( policy.claim( now + std::chrono::milliseconds { 124 } ) == std::chrono::milliseconds { 1 } );
	}

	const auto idle { start + std::chrono::seconds { 60 } };
	REQUIRE_FALSE( policy.claim( idle ).has_value() );
	CHECK( policy.claim( idle ) == std::chrono::milliseconds { 125 } );
}

TEST_CASE( "Spread rates round intervals up and preserve spacing across refreshes", "[downloader][lane]" )
{
	const LaneSettings settings { .rate = RequestRate { 3, 1, true } };
	LanePolicy policy { "host", settings };
	const Clock::time_point start { std::chrono::seconds { 100 } };
	const auto interval { std::chrono::ceil< Clock::duration >( std::chrono::duration< double > { 1.0 / 3.0 } ) };
	REQUIRE_FALSE( policy.claim( start ).has_value() );
	policy.configure( settings );
	CHECK( policy.claim( start ) == interval );
	CHECK( policy.claim( start + interval - Clock::duration { 1 } ) == Clock::duration { 1 } );
	REQUIRE_FALSE( policy.claim( start + interval ).has_value() );
}

TEST_CASE( "429 reductions increase spread intervals and retain cooldowns", "[downloader][lane]" )
{
	LanePolicy policy { "host", LaneSettings { .rate = RequestRate { 3, 1, true } } };
	const Clock::time_point start { std::chrono::seconds { 100 } };
	policy.limited( std::string_view { "5" }, start );
	CHECK( policy.settings().rate == ( RequestRate { 2, 1, true } ) );
	CHECK( policy.claim( start ) == std::chrono::seconds { 5 } );
	const auto resumed { start + std::chrono::seconds { 5 } };
	REQUIRE_FALSE( policy.claim( resumed ).has_value() );
	CHECK( policy.claim( resumed ) == std::chrono::milliseconds { 500 } );
	policy.limited( std::string_view { "1" }, resumed );
	CHECK( policy.settings().rate == ( RequestRate { 1, 1, true } ) );
	REQUIRE_FALSE( policy.claim( resumed + std::chrono::seconds { 1 } ).has_value() );
	CHECK( policy.claim( resumed + std::chrono::seconds { 1 } ) == std::chrono::seconds { 1 } );
}

TEST_CASE( "A slot reopens one window after its own request started", "[downloader][lane]" )
{
	auto policy { rateLimited( 3, 1 ) };
	const Clock::time_point start { std::chrono::seconds { 100 } };
	const auto first { start };
	const auto second { start + std::chrono::milliseconds { 400 } };
	const auto third { start + std::chrono::milliseconds { 800 } };

	REQUIRE_FALSE( policy.claim( first ).has_value() );
	REQUIRE_FALSE( policy.claim( second ).has_value() );
	REQUIRE_FALSE( policy.claim( third ).has_value() );

	CHECK( policy.claim( third ) == std::chrono::milliseconds { 200 } );

	const auto reopened { first + std::chrono::seconds { 1 } };
	CHECK( policy.claim( reopened - Clock::duration { 1 } ) == Clock::duration { 1 } );
	REQUIRE_FALSE( policy.claim( reopened ).has_value() );

	CHECK( policy.nextSlot() == second + std::chrono::seconds { 1 } );
}

TEST_CASE( "Claiming a slot late moves that slot's reopening with it", "[downloader][lane]" )
{
	auto policy { rateLimited( 1, 1 ) };
	const Clock::time_point start { std::chrono::seconds { 100 } };
	constexpr Clock::duration jitter { std::chrono::milliseconds { 40 } };

	REQUIRE_FALSE( policy.claim( start ).has_value() );
	REQUIRE_FALSE( policy.claim( start + std::chrono::seconds { 1 } + jitter ).has_value() );

	CHECK( policy.nextSlot() == start + std::chrono::seconds { 2 } + jitter );
}

TEST_CASE( "No window ever holds more than the configured requests", "[downloader][lane]" )
{
	auto policy { rateLimited( 4, 1 ) };
	constexpr Clock::duration window { std::chrono::seconds { 1 } };
	Clock::time_point now { std::chrono::seconds { 100 } };
	std::vector< Clock::time_point > starts {};

	for ( int step = 0; step < 5000; ++step )
	{
		if ( !policy.claim( now ).has_value() ) starts.push_back( now );

		now += std::chrono::milliseconds { 1 + ( step * 7 ) % 13 };
	}

	REQUIRE( starts.size() > 4 );

	for ( std::size_t index { 4 }; index < starts.size(); ++index )
		CHECK( starts[ index ] - starts[ index - 4 ] >= window );
}

TEST_CASE( "An idle lane gets back its allowance and nothing more", "[downloader][lane]" )
{
	auto policy { rateLimited( 2, 1 ) };
	const Clock::time_point start { std::chrono::seconds { 100 } };

	REQUIRE_FALSE( policy.claim( start ).has_value() );

	const auto idled { start + std::chrono::seconds { 60 } };
	REQUIRE_FALSE( policy.claim( idled ).has_value() );
	REQUIRE_FALSE( policy.claim( idled ).has_value() );

	CHECK( policy.claim( idled ) == std::chrono::seconds { 1 } );
}

TEST_CASE( "Clearing a lane's backoff forgets the requests in its window", "[downloader][lane]" )
{
	auto policy { rateLimited( 2, 30 ) };
	const Clock::time_point start { std::chrono::seconds { 100 } };

	REQUIRE_FALSE( policy.claim( start ).has_value() );
	REQUIRE_FALSE( policy.claim( start ).has_value() );
	REQUIRE( policy.claim( start ).has_value() );

	policy.reset();

	REQUIRE_FALSE( policy.claim( start ).has_value() );
	REQUIRE_FALSE( policy.claim( start ).has_value() );
	CHECK( policy.claim( start ) == std::chrono::seconds { 30 } );
}

TEST_CASE( "Clearing a lane's backoff moves its generation", "[downloader][lane]" )
{
	auto policy { rateLimited( 1, 1 ) };
	const std::uint64_t initial { policy.generation() };

	policy.limited( std::nullopt );
	CHECK( policy.generation() == initial );

	policy.reset();
	CHECK( policy.generation() > initial );

	const std::uint64_t cleared { policy.generation() };
	policy.configure( LaneSettings { .rate = RequestRate { 1, 2 } } );
	CHECK( policy.generation() > cleared );
}

TEST_CASE( "429 fallback doubles to 60 seconds then disables until explicit reset", "[downloader][lane][429-recovery]" )
{
	using namespace std::chrono_literals;
	auto policy { rateLimited( 8, 1 ) };
	Clock::time_point now { 100s };
	for ( const auto delay : { 1s, 2s, 4s, 8s, 16s, 32s, 60s } )
	{
		REQUIRE( policy.limited( std::nullopt, now ) == delay );
		REQUIRE_FALSE( policy.disabled() );
		REQUIRE( policy.claim( now ) == delay );
		// Already in-flight responses share the pause rather than exhausting every retry immediately.
		REQUIRE( policy.limited( std::nullopt, now ) == delay );
		REQUIRE_FALSE( policy.disabled() );
		now += delay;
	}
	policy.limited( std::nullopt, now );
	REQUIRE( policy.disabled() );
	REQUIRE( policy.claim( now + 24h ) == Clock::duration::max() );
	policy.configure( LaneSettings { .rate = RequestRate { 8, 1 } } );
	for ( int index {}; index < 100; ++index ) policy.succeeded( now + 24h );

	REQUIRE( policy.disabled() );
	LaneSnapshot snapshot;
	policy.fill( snapshot, now + 24h );
	REQUIRE( snapshot.disabled );
	REQUIRE( snapshot.remaining == Clock::duration::zero() );
	policy.reset();
	REQUIRE_FALSE( policy.disabled() );
	REQUIRE_FALSE( policy.claim( now ).has_value() );
	REQUIRE( policy.limited( std::nullopt, now ) == 1s );
}

TEST_CASE(
	"429 honors explicit Retry-After and treats malformed headers as fallback",
	"[downloader][lane][429-recovery]" )
{
	using namespace std::chrono_literals;
	auto policy { unlimited( 30s ) };
	const Clock::time_point start { 100s };
	REQUIRE( policy.limited( std::string_view { "invalid" }, start ) == 1s );
	REQUIRE( policy.limited( std::string_view { "invalid" }, start + 1s ) == 2s );
	REQUIRE( policy.limited( std::string_view { "120" }, start + 3s ) == 120s );
	REQUIRE_FALSE( policy.disabled() );
}

TEST_CASE(
	"Ten consecutive successes gradually recover only up to the configured allowance",
	"[downloader][lane][429-recovery]" )
{
	using namespace std::chrono_literals;
	LanePolicy policy { "host", LaneSettings { .rate = RequestRate { 4, 1, true, 2 } } };
	const Clock::time_point start { 100s };
	policy.limited( std::string_view { "1" }, start );
	policy.limited( std::string_view { "1" }, start );
	REQUIRE( policy.settings().rate == ( RequestRate { 2, 1, true, 2 } ) );
	// Responses already in flight during cooldown do not recover the rate.
	for ( int index {}; index < 20; ++index ) policy.succeeded( start );

	REQUIRE( policy.settings().rate->requests == 2 );
	for ( int index {}; index < 9; ++index ) policy.succeeded( start + 1s );

	REQUIRE( policy.settings().rate->requests == 2 );
	policy.unsuccessful();
	policy.succeeded( start + 1s );
	REQUIRE( policy.settings().rate->requests == 2 );
	for ( int index {}; index < 9; ++index ) policy.succeeded( start + 1s );

	REQUIRE( policy.settings().rate == ( RequestRate { 3, 1, true, 2 } ) );
	for ( int index {}; index < 30; ++index ) policy.succeeded( start + 1s );

	REQUIRE( policy.settings().rate == ( RequestRate { 4, 1, true, 2 } ) );
	policy.configure( LaneSettings { .rate = RequestRate { 4, 1, true, 2 } } );
	REQUIRE( policy.settings().rate->requests == 4 );
	REQUIRE( policy.limited( std::nullopt, start + 2s ) == 1s );
}

TEST_CASE( "Successful recovery restarts the exponential delay at one second", "[downloader][lane][429-recovery]" )
{
	using namespace std::chrono_literals;
	auto policy { rateLimited( 4, 1 ) };
	const Clock::time_point start { 100s };
	REQUIRE( policy.limited( std::nullopt, start ) == 1s );
	REQUIRE( policy.limited( std::nullopt, start + 1s ) == 2s );
	for ( int index {}; index < 10; ++index ) policy.succeeded( start + 3s );

	REQUIRE( policy.settings().rate->requests == 3 );
	REQUIRE( policy.limited( std::nullopt, start + 4s ) == 1s );
}
