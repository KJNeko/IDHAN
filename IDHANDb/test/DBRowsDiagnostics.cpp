#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>

#include "idhan/db/DBResult.hpp"

using namespace idhan::db;

static DBResult distanceResult()
{
	DBResult result { PQmakeEmptyPGresult( nullptr, PGRES_TUPLES_OK ) };
	PGresAttDesc column {};
	char name[] { "distance" };
	column.name = name;
	column.typid = 21;
	column.typlen = 2;
	column.format = 1;
	REQUIRE( PQsetResultAttrs( result.get(), 1, &column ) == 1 );
	return result;
}

TEST_CASE( "Row type mismatches identify the query and column", "[db][decode]" )
{
	const std::string sql { "SELECT distance FROM hamming_distance" };
	try
	{
		DBRows< std::int32_t > rows { distanceResult(), sql };
		FAIL( "Expected an OID mismatch" );
	}
	catch ( const DBDecodeError& error )
	{
		CHECK( error.column == 0 );
		CHECK( error.expected_oid == 23 );
		CHECK( error.actual_oid == 21 );
		const std::string message { error.what() };
		CHECK( message.find( "distance" ) != std::string::npos );
		CHECK( message.find( sql ) != std::string::npos );
	}
}

TEST_CASE( "Lazy row decoding retains query context after a move", "[db][decode]" )
{
	auto raw { distanceResult() };
	REQUIRE( PQsetvalue( raw.get(), 0, 0, nullptr, -1 ) == 1 );
	DBRows< std::int16_t > original { std::move( raw ), "SELECT distance FROM hamming_distance" };
	auto rows { std::move( original ) };
	try
	{
		(void)rows.row( 0 );
		FAIL( "Expected a non-optional NULL error" );
	}
	catch ( const DBDecodeError& error )
	{
		const std::string message { error.what() };
		CHECK( message.find( "NULL column" ) != std::string::npos );
		CHECK( message.find( "distance" ) != std::string::npos );
		CHECK( message.find( "row 0" ) != std::string::npos );
		CHECK( message.find( "SELECT distance FROM hamming_distance" ) != std::string::npos );
	}
}

TEST_CASE( "Smallint distance decodes without widening its OID", "[db][decode]" )
{
	auto raw { distanceResult() };
	char value[] { 0, 8 };
	REQUIRE( PQsetvalue( raw.get(), 0, 0, value, 2 ) == 1 );
	DBRows< std::int16_t > rows { std::move( raw ) };
	CHECK( std::get< 0 >( rows.row( 0 ) ) == 8 );
}
