#include "http/IoPool.hpp"
#include "http/Lane.hpp"
#include "http/LanePolicy.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace idhan::downloader;

TEST_CASE( "Lane download speed aggregates received bytes and expires idle samples", "[downloader][lane]" )
{
	IoPool pool { 1 };
	LanePolicy policy { "host", LaneSettings {} };
	auto lane { std::make_shared< Lane >( "host", policy, pool, Lane::Config {} ) };
	using Clock = std::chrono::steady_clock;
	const Clock::time_point start { std::chrono::seconds { 100 } };
	LaneSnapshot snapshot {};
	lane->fill( snapshot, start );
	CHECK( snapshot.download_bytes_per_second == 0 );

	lane->onBytesReceived( 1024, start );
	lane->onBytesReceived( 2048, start + std::chrono::milliseconds { 50 } );
	lane->onBytesReceived( 4096, start + std::chrono::milliseconds { 500 } );
	lane->fill( snapshot, start + std::chrono::milliseconds { 500 } );
	CHECK( snapshot.download_bytes_per_second == 7168 );

	lane->fill( snapshot, start + std::chrono::seconds { 1 } );
	CHECK( snapshot.download_bytes_per_second == 4096 );

	lane->onBytesReceived( 512, start + std::chrono::seconds { 1 } );
	lane->fill( snapshot, start + std::chrono::seconds { 1 } );
	CHECK( snapshot.download_bytes_per_second == 4608 );

	lane->fill( snapshot, start + std::chrono::seconds { 2 } );
	CHECK( snapshot.download_bytes_per_second == 0 );

	lane->onBytesReceived( 256, start + std::chrono::seconds { 30 } );
	lane->fill( snapshot, start + std::chrono::seconds { 30 } );
	CHECK( snapshot.download_bytes_per_second == 256 );
}
