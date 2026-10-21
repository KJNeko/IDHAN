#include "IDHANDB.hpp"

// Folly includes glog's CHECK; install Catch's assertion macros afterward.
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

TEST_CASE( "PQ parameters use PostgreSQL wire representations", "[pg][params]" )
{
	using idhan::db::createParamValue;

	const auto integer { createParamValue( std::int32_t { 0x01020304 } ) };
	CHECK( integer.m_format == 1 );
	CHECK(
		integer.m_binary
		== std::vector< std::byte > {
			std::byte { 0x01 },
			std::byte { 0x02 },
			std::byte { 0x03 },
			std::byte { 0x04 },
		} );

	const auto negative { createParamValue( std::int16_t { -2 } ) };
	CHECK( negative.m_binary == std::vector< std::byte > { std::byte { 0xff }, std::byte { 0xfe } } );

	const auto text { createParamValue( std::string_view { "actual text" } ) };
	CHECK( text.m_format == 0 );
	CHECK( text.m_text == "actual text" );

	const auto null { createParamValue( std::optional< std::int64_t > {} ) };
	CHECK( null.m_is_null );
	CHECK( null.data() == nullptr );

	STATIC_CHECK( idhan::db::defineParamType< std::vector< std::int16_t > >() == 1005 );
	STATIC_CHECK( idhan::db::defineParamType< std::vector< std::int32_t > >() == 1007 );
	STATIC_CHECK( idhan::db::defineParamType< std::vector< std::int64_t > >() == 1016 );
	STATIC_CHECK( idhan::db::defineParamType< std::vector< std::string > >() == 1009 );

	const auto array { createParamValue( std::vector< std::int16_t > { 1, 258 } ) };
	CHECK( array.m_format == 1 );
	CHECK(
		array.m_binary
		== std::vector< std::byte > {
			std::byte { 0 }, std::byte { 0 }, std::byte { 0 }, std::byte { 1 }, // dimensions
			std::byte { 0 }, std::byte { 0 }, std::byte { 0 }, std::byte { 0 }, // has null
			std::byte { 0 }, std::byte { 0 }, std::byte { 0 }, std::byte { 21 }, // int2 OID
			std::byte { 0 }, std::byte { 0 }, std::byte { 0 }, std::byte { 2 }, // length
			std::byte { 0 }, std::byte { 0 }, std::byte { 0 }, std::byte { 1 }, // lower bound
			std::byte { 0 }, std::byte { 0 }, std::byte { 0 }, std::byte { 2 },  std::byte { 0 }, std::byte { 1 },
			std::byte { 0 }, std::byte { 0 }, std::byte { 0 }, std::byte { 2 },  std::byte { 1 }, std::byte { 2 },
		} );

	const auto nullable_array { createParamValue( std::vector< std::optional< std::int32_t > > { 7, std::nullopt } ) };
	CHECK( nullable_array.m_binary[ 7 ] == std::byte { 1 } );
	CHECK( nullable_array.m_binary[ nullable_array.m_binary.size() - 1 ] == std::byte { 0xff } );

	const auto text_array { createParamValue( std::vector< std::string > { "one", "two" } ) };
	CHECK( text_array.m_format == 1 );
	CHECK( text_array.m_binary.size() == 34 );
}

TEST_CASE( "PGConnect", "[pg]" )
{
	idhan::db::IDHANPQContext ctx { "localhost", 5432, "idhan-db" };

	auto task { ctx.execSql( "SELECT * FROM pg_type WHERE oid = $1", 23 ) };
	CHECK( task.socket() >= 0 );

	const auto deadline { std::chrono::steady_clock::now() + std::chrono::seconds { 5 } };
	idhan::db::DBTaskState state { task.poll() };
	while ( state != idhan::db::DBTaskState::RESULT_READY )
	{
		REQUIRE( std::chrono::steady_clock::now() < deadline );
		std::this_thread::yield();
		state = task.poll();
	}

	auto result { task.nextResult() };
	REQUIRE( result != nullptr );
	CHECK( PQresultStatus( result.get() ) == PGRES_TUPLES_OK );
	CHECK( task.nextResult() == nullptr );
}
