#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

#include "gui/hydrus/ImportProgress.hpp"

namespace idhan::test
{

using hydrus::ImportProgress;
using namespace std::chrono_literals;

ImportProgress::TimePoint importerProgressTime( const std::chrono::seconds elapsed )
{
	return ImportProgress::TimePoint {} + elapsed;
}

TEST_CASE( "Importer needs completed work before estimating a phase", "[importer][progress]" )
{
	ImportProgress progress;
	CHECK_FALSE( progress.snapshot().remaining );
	progress.start( 10000, importerProgressTime( 0s ) );
	const auto waiting = progress.snapshot( importerProgressTime( 30s ) );
	CHECK( waiting.recent_per_second == 0 );
	CHECK( waiting.average_per_second == 0 );
	CHECK_FALSE( waiting.remaining );
}

TEST_CASE( "Importer includes the first batch's full processing time", "[importer][progress]" )
{
	ImportProgress progress;
	progress.start( 10000, importerProgressTime( 0s ) );
	progress.advance( 1000, importerProgressTime( 10s ) );
	const auto first = progress.snapshot( importerProgressTime( 10s ) );
	CHECK( first.recent_per_second == Catch::Approx( 100 ) );
	CHECK( first.average_per_second == Catch::Approx( 100 ) );
	REQUIRE( first.remaining );
	CHECK( *first.remaining == 90s );

	const auto pending = progress.snapshot( importerProgressTime( 20s ) );
	CHECK( pending.recent_per_second == Catch::Approx( 50 ) );
	CHECK( pending.average_per_second == Catch::Approx( 50 ) );
	REQUIRE( pending.remaining );
	CHECK( *pending.remaining == 180s );
}

TEST_CASE( "Importer distinguishes recent rate from the phase average", "[importer][progress]" )
{
	ImportProgress progress;
	progress.start( 30000, importerProgressTime( 0s ) );
	progress.advance( 6000, importerProgressTime( 60s ) );
	progress.advance( 12000, importerProgressTime( 120s ) );
	const auto snapshot = progress.snapshot( importerProgressTime( 120s ) );
	CHECK( snapshot.recent_per_second == Catch::Approx( 200 ) );
	CHECK( snapshot.average_per_second == Catch::Approx( 150 ) );
	REQUIRE( snapshot.remaining );
	CHECK( *snapshot.remaining == 60s );
}

TEST_CASE( "Importer prorates batches that cross the recent window", "[importer][progress]" )
{
	ImportProgress progress;
	progress.start( 24000, importerProgressTime( 0s ) );
	progress.advance( 12000, importerProgressTime( 120s ) );
	const auto snapshot = progress.snapshot( importerProgressTime( 120s ) );
	CHECK( snapshot.recent_per_second == Catch::Approx( 100 ) );
	CHECK( snapshot.average_per_second == Catch::Approx( 100 ) );
	REQUIRE( snapshot.remaining );
	CHECK( *snapshot.remaining == 120s );
}

TEST_CASE( "Importer ages out completed work during a server wait", "[importer][progress]" )
{
	ImportProgress progress;
	progress.start( 2000, importerProgressTime( 0s ) );
	progress.advance( 1000, importerProgressTime( 10s ) );
	const auto fading = progress.snapshot( importerProgressTime( 65s ) );
	CHECK( fading.recent_per_second == Catch::Approx( 500.0 / 60.0 ) );
	CHECK( fading.average_per_second == Catch::Approx( 1000.0 / 65.0 ) );

	const auto stalled = progress.snapshot( importerProgressTime( 80s ) );
	CHECK( stalled.recent_per_second == 0 );
	CHECK( stalled.average_per_second == Catch::Approx( 12.5 ) );
	CHECK_FALSE( stalled.remaining );
}

TEST_CASE( "Importer resets measurements for each phase", "[importer][progress]" )
{
	ImportProgress progress;
	progress.start( 10000, importerProgressTime( 0s ) );
	progress.advance( 10000, importerProgressTime( 10s ) );
	progress.start( 100, importerProgressTime( 40s ) );
	const auto fresh = progress.snapshot( importerProgressTime( 42s ) );
	CHECK( fresh.processed == 0 );
	CHECK( fresh.total == 100 );
	CHECK( fresh.recent_per_second == 0 );
	CHECK_FALSE( fresh.remaining );

	progress.advance( 2, importerProgressTime( 44s ) );
	const auto aliases = progress.snapshot( importerProgressTime( 44s ) );
	CHECK( aliases.recent_per_second == Catch::Approx( 0.5 ) );
	CHECK( aliases.average_per_second == Catch::Approx( 0.5 ) );
	REQUIRE( aliases.remaining );
	CHECK( *aliases.remaining == 196s );
}

TEST_CASE( "Importer handles empty work and clamps excess progress", "[importer][progress]" )
{
	ImportProgress progress;
	progress.start( 0, importerProgressTime( 0s ) );
	CHECK( progress.snapshot( importerProgressTime( 0s ) ).remaining == 0s );
	progress.start( 10, importerProgressTime( 0s ) );
	progress.advance( std::numeric_limits< std::size_t >::max(), importerProgressTime( 1s ) );
	progress.advance( 1, importerProgressTime( 2s ) );
	const auto complete = progress.snapshot( importerProgressTime( 3s ) );
	CHECK( complete.processed == 10 );
	CHECK( complete.remaining == 0s );
}

TEST_CASE( "Importer handles zero elapsed time and rates beyond int range", "[importer][progress]" )
{
	ImportProgress progress;
	const ImportProgress::TimePoint start {};
	progress.start( 200, start );
	progress.advance( 100, start );
	const auto immediate = progress.snapshot( start );
	CHECK( immediate.recent_per_second == 0 );
	CHECK( immediate.average_per_second == 0 );
	CHECK_FALSE( immediate.remaining );
	CHECK( progress.snapshot( start + 1s ).recent_per_second == Catch::Approx( 100 ) );

	progress.start( 200, start );
	progress.advance( 100, start + 1ns );
	const auto fast = progress.snapshot( start + 1ns );
	CHECK( fast.recent_per_second == Catch::Approx( 100000000000.0 ) );
	CHECK( std::isfinite( fast.average_per_second ) );
	CHECK( fast.remaining == 1s );
}

TEST_CASE( "Importer ignores empty updates when timing batches", "[importer][progress]" )
{
	ImportProgress progress;
	progress.start( 1000, importerProgressTime( 0s ) );
	progress.advance( 0, importerProgressTime( 5s ) );
	progress.advance( 100, importerProgressTime( 10s ) );
	CHECK( progress.snapshot( importerProgressTime( 10s ) ).recent_per_second == Catch::Approx( 10 ) );
}

} // namespace idhan::test
