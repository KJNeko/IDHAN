#include <catch2/catch_test_macros.hpp>
#include "ServerFixture.hpp"

namespace idhan::test
{

TEST_CASE_METHOD( ServerFixture, "Parser hosts expose provenance and support removal", "[api][downloader][hosts]" )
{
	{
		pqxx::work tx { db() };
		tx.exec(
			//language=postgresql
			R"SQL(
				INSERT INTO downloader_parser_hosts (host, parser, auto_discovered)
				VALUES
				    ('auto.example', 'hyshare', true),
				    ('manual.example', 'hyshare', false)
			)SQL" );
		tx.commit();
	}
	const auto listed { api().get( "/downloader/hosts" ) };
	REQUIRE( listed.status == 200 );
	REQUIRE( listed.json.isArray() );
	REQUIRE( listed.json.size() == 2 );
	CHECK( listed.json[ 0 ][ "host" ].asString() == "auto.example" );
	CHECK( listed.json[ 0 ][ "parser" ].asString() == "hyshare" );
	CHECK( listed.json[ 0 ][ "auto_discovered" ].asBool() );
	CHECK_FALSE( listed.json[ 1 ][ "auto_discovered" ].asBool() );

	REQUIRE( api().del( "/downloader/hosts", { { "host", "AUTO.EXAMPLE" } } ).status == 200 );
	const auto remaining { api().get( "/downloader/hosts" ) };
	REQUIRE( remaining.status == 200 );
	REQUIRE( remaining.json.size() == 1 );
	CHECK( remaining.json[ 0 ][ "host" ].asString() == "manual.example" );
}

TEST_CASE_METHOD( ServerFixture, "Parser host management rejects invalid inputs", "[api][downloader][hosts]" )
{
	Json::Value body { Json::objectValue };
	body[ "host" ] = "https://share.example/path";
	body[ "parser" ] = "hyshare";
	CHECK( api().post( "/downloader/hosts", body ).status == 400 );
	body[ "host" ] = 42;
	CHECK( api().post( "/downloader/hosts", body ).status == 400 );
	CHECK( api().del( "/downloader/hosts" ).status == 400 );
	CHECK( api().del( "/downloader/hosts", { { "host", "bad.example:123" } } ).status == 400 );
}

} // namespace idhan::test
