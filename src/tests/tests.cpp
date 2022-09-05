//
// Created by kj16609 on 9/2/22.
//

#include <catch2/catch_test_macros.hpp>

#include <spdlog/spdlog.h>


#include <idhan/RecursiveConnection.hpp>


TEST_CASE("Connection ctor db not ready", "[database][ctor][connection]")
{
	REQUIRE_THROWS(idhan::database::Connection());
}

TEST_CASE("Connection ctor db ready", "[database][ctor][connection]")
{
	SECTION( "Init" )
	{
		try
		{
			idhan::database::ConnectionPool::init( "host=localhost dbname=template1 user=postgres password=idhan_tester" );
		}
		catch ( ... )
		{
			FAIL(
			  "Failed to log into template1. Please ensure postgresql is installed and running along with user 'postgres' being available with no password" );
		}
	}

	SECTION("Test")
	{
		REQUIRE_NOTHROW( idhan::database::Connection() );
	}

	SECTION("Deinit")
	{
		REQUIRE_NOTHROW(idhan::database::ConnectionPool::deinit());
	}
}






