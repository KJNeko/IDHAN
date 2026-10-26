#include "threading/SingleRunGuard.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <stdexcept>
#include <thread>
#include <vector>

namespace idhan::test
{

TEST_CASE( "A running sweep rejects concurrent starts without releasing its gate", "[sweep-gate]" )
{
	std::atomic< bool > running { false };
	{
		SingleRunGuard owner { running };
		REQUIRE( owner.acquired() );
		std::atomic< int > unexpected_starts { 0 };
		{
			std::vector< std::jthread > contenders {};
			for ( int i { 0 }; i < 16; ++i )
				contenders.emplace_back(
					[ &running, &unexpected_starts ]()
					{
						SingleRunGuard contender { running };
						if ( contender.acquired() ) ++unexpected_starts;
					} );
		}
		CHECK( unexpected_starts.load() == 0 );
		CHECK( running.load() );
	}
	SingleRunGuard next_run { running };
	CHECK( next_run.acquired() );
}

TEST_CASE( "Sweep gates are independent and release on failure", "[sweep-gate]" )
{
	std::atomic< bool > generation_running { false };
	std::atomic< bool > distance_running { false };
	SingleRunGuard generation { generation_running };
	REQUIRE( generation.acquired() );
	CHECK_THROWS_AS(
		[ &distance_running ]()
		{
			SingleRunGuard distance { distance_running };
			REQUIRE( distance.acquired() );
			throw std::runtime_error( "batch failed" );
		}(),
		std::runtime_error );
	CHECK( generation_running.load() );
	SingleRunGuard retry { distance_running };
	CHECK( retry.acquired() );
}

} // namespace idhan::test
