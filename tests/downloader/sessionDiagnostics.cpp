#include "SessionDiagnostics.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace idhan::downloader;

static WorkInfo work( const WorkID id, std::string url = "https://example.invalid/a" )
{
	return WorkInfo { .id = id, .parent = std::nullopt, .url = std::move( url ) };
}

TEST_CASE( "Counters total every observed notification", "[downloader][diagnostics]" )
{
	SessionDiagnostics diagnostics {};

	diagnostics.recordStarted( work( 1 ) );
	diagnostics.recordStarted( work( 2 ) );
	diagnostics.recordCompleted( work( 1 ) );
	diagnostics.recordFailed( work( 2 ), "boom" );
	diagnostics.recordRequest( RequestInfo { .work = 1, .url = "https://a", .lane = "a", .status = 200, .bytes = 40 } );
	diagnostics.recordRequest( RequestInfo { .work = 1, .url = "https://b", .lane = "a", .status = 404, .bytes = 2 } );
	diagnostics.recordImported( ImportInfo { .work = 1, .url = "https://c", .record_id = 7, .size = 100 } );
	diagnostics.recordImportFailed( 1, "https://d", "no file" );

	const SessionCounters counters { diagnostics.snapshot( 0 ).counters };

	CHECK( counters.work_started == 2 );
	CHECK( counters.work_completed == 1 );
	CHECK( counters.work_failed == 1 );
	CHECK( counters.requests == 2 );
	CHECK( counters.request_bytes == 42 );
	CHECK( counters.imported == 1 );
	CHECK( counters.import_bytes == 100 );
	CHECK( counters.import_failed == 1 );
}

TEST_CASE( "Each follow outcome lands in its own counter", "[downloader][diagnostics]" )
{
	SessionDiagnostics diagnostics {};
	const WorkInfo followed { .id = 0, .parent = WorkID { 1 }, .url = "https://example.invalid/b" };

	diagnostics.recordFollowed( followed, FollowStatus::QUEUED );
	diagnostics.recordFollowed( followed, FollowStatus::QUEUED );
	diagnostics.recordFollowed( followed, FollowStatus::FILTERED );
	diagnostics.recordFollowed( followed, FollowStatus::ALREADY_QUEUED );
	diagnostics.recordFollowed( followed, FollowStatus::ALREADY_EXPLORED );
	diagnostics.recordFollowed( followed, FollowStatus::ALREADY_IMPORTED );

	const SessionCounters counters { diagnostics.snapshot( 0 ).counters };

	CHECK( counters.follows_queued == 2 );
	CHECK( counters.follows_filtered == 1 );
	CHECK( counters.follows_already_queued == 1 );
	CHECK( counters.follows_already_explored == 1 );
	CHECK( counters.follows_already_imported == 1 );
}

TEST_CASE( "A follow event is attributed to the script that followed it", "[downloader][diagnostics]" )
{
	SessionDiagnostics diagnostics {};

	diagnostics.recordFollowed(
		WorkInfo { .id = 0, .parent = WorkID { 9 }, .url = "https://x" }, FollowStatus::QUEUED );

	const auto events { diagnostics.snapshot( 0 ).events };

	REQUIRE( events.size() == 1 );
	CHECK( events[ 0 ].work == 9 );
	CHECK( events[ 0 ].detail == "queued" );
}

TEST_CASE( "Sequences are monotonic and a cursor takes only what follows it", "[downloader][diagnostics]" )
{
	SessionDiagnostics diagnostics {};

	for ( WorkID id { 1 }; id <= 5; ++id ) diagnostics.recordStarted( work( id ) );

	const SessionSnapshot all { diagnostics.snapshot( 0 ) };

	REQUIRE( all.events.size() == 5 );
	CHECK( all.event_sequence == 5 );
	CHECK( all.events_dropped == 0 );

	for ( std::size_t index { 1 }; index < all.events.size(); ++index )
		CHECK( all.events[ index ].sequence > all.events[ index - 1 ].sequence );

	const SessionSnapshot tail { diagnostics.snapshot( all.events[ 2 ].sequence ) };

	REQUIRE( tail.events.size() == 2 );
	CHECK( tail.events[ 0 ].work == 4 );
	CHECK( tail.events[ 1 ].work == 5 );

	CHECK( diagnostics.snapshot( all.event_sequence ).events.empty() );
}

TEST_CASE( "A reader that falls behind the ring is told how much it missed", "[downloader][diagnostics]" )
{
	SessionDiagnostics diagnostics {};

	diagnostics.recordStarted( work( 1 ) );
	const SessionSnapshot first { diagnostics.snapshot( 0 ) };
	REQUIRE( first.event_sequence == 1 );

	for ( WorkID id { 2 }; id <= 401; ++id ) diagnostics.recordStarted( work( id ) );

	const SessionSnapshot behind { diagnostics.snapshot( first.event_sequence ) };

	CHECK( behind.events.size() == 256 );
	CHECK( behind.events_dropped == 144 );
	CHECK( behind.event_sequence == 401 );

	CHECK( diagnostics.snapshot( behind.event_sequence ).events_dropped == 0 );
}

TEST_CASE( "A fresh reader is never told it missed events", "[downloader][diagnostics]" )
{
	SessionDiagnostics diagnostics {};

	for ( WorkID id { 1 }; id <= 400; ++id ) diagnostics.recordStarted( work( id ) );

	const SessionSnapshot fresh { diagnostics.snapshot( 0 ) };

	CHECK( fresh.events.size() == 256 );
	CHECK( fresh.events_dropped == 0 );
}

TEST_CASE( "Published loop state is what a snapshot reports", "[downloader][diagnostics]" )
{
	SessionDiagnostics diagnostics {};

	CHECK( diagnostics.snapshot( 0 ).running == 0 );

	SessionDiagnostics::LoopState state {};
	state.queued = 31;
	state.in_flight_requests = 9;
	state.work.emplace_back(
		WorkSnapshot { .id = 204, .url = "https://example.invalid/post", .url_class = "post", .parser = "parsePost" } );
	state.requests.emplace_back(
		PendingRequestSnapshot { .work = 204, .url = "https://example.invalid/f.jpg", .lane = "example.invalid" } );
	diagnostics.publish( std::move( state ) );

	const SessionSnapshot snapshot { diagnostics.snapshot( 0 ) };

	CHECK( snapshot.queued == 31 );
	CHECK( snapshot.running == 1 );
	CHECK( snapshot.in_flight_requests == 9 );
	REQUIRE( snapshot.work.size() == 1 );
	CHECK( snapshot.work[ 0 ].url_class == "post" );
	REQUIRE( snapshot.requests.size() == 1 );
	CHECK( snapshot.requests[ 0 ].lane == "example.invalid" );
}

TEST_CASE( "Publishing loop state leaves the event ring alone", "[downloader][diagnostics]" )
{
	SessionDiagnostics diagnostics {};

	diagnostics.recordStarted( work( 1 ) );
	diagnostics.publish( SessionDiagnostics::LoopState { .queued = 4 } );

	const SessionSnapshot snapshot { diagnostics.snapshot( 0 ) };

	CHECK( snapshot.queued == 4 );
	CHECK( snapshot.events.size() == 1 );
	CHECK( snapshot.event_sequence == 1 );
}
