#include "http/IoPool.hpp"
#include "http/Lane.hpp"
#include "http/LanePolicy.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace idhan::downloader;

TEST_CASE( "Lane download speed uses one-second differences in cumulative bytes", "[downloader][lane]" )
{
	IoPool pool { 1 };
	LanePolicy policy { "host", LaneSettings {} };
	auto lane { std::make_shared< Lane >( "host", policy, pool, Lane::Config {} ) };
	using Clock = std::chrono::steady_clock;
	const Clock::time_point start { std::chrono::seconds { 100 } };
	LaneSnapshot snapshot {};
	lane->fill( snapshot, start );
	CHECK( snapshot.download_bytes_per_second == 0 );

	lane->onBytesReceived( 1024 );
	lane->onBytesReceived( 2048 );
	lane->onBytesReceived( 4096 );
	lane->fill( snapshot, start + std::chrono::milliseconds { 500 } );
	CHECK( snapshot.download_bytes_per_second == 0 );

	lane->fill( snapshot, start + std::chrono::seconds { 1 } );
	CHECK( snapshot.download_bytes_per_second == 7168 );

	lane->onBytesReceived( 512 );
	lane->fill( snapshot, start + std::chrono::seconds { 1 } );
	CHECK( snapshot.download_bytes_per_second == 7168 );

	lane->fill( snapshot, start + std::chrono::seconds { 2 } );
	CHECK( snapshot.download_bytes_per_second == 512 );

	lane->fill( snapshot, start + std::chrono::seconds { 3 } );
	CHECK( snapshot.download_bytes_per_second == 0 );
	lane->onBytesReceived( 256 );
	lane->fill( snapshot, start + std::chrono::seconds { 30 } );
	CHECK( snapshot.download_bytes_per_second == 9 );
}

TEST_CASE( "Download speed normalizes counter deltas over complete sampling intervals", "[downloader][bandwidth]" )
{
	using Clock = std::chrono::steady_clock;
	const Clock::time_point start { std::chrono::seconds { 100 } };
	DownloadSpeed speed { start };
	CHECK( speed.sample( 500, start + std::chrono::milliseconds { 500 } ) == 0 );
	CHECK( speed.sample( 1000, start + std::chrono::seconds { 1 } ) == 1000 );
	CHECK( speed.sample( 1000, start + std::chrono::milliseconds { 1500 } ) == 1000 );
	CHECK( speed.sample( 1000, start + std::chrono::seconds { 2 } ) == 0 );
	CHECK( speed.sample( 5000, start + std::chrono::seconds { 4 } ) == 2000 );
	CHECK( speed.sample( 10, start + std::chrono::seconds { 5 } ) == 0 );
	CHECK( speed.sample( 110, start + std::chrono::seconds { 6 } ) == 100 );
}
