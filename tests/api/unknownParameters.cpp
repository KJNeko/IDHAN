#include <catch2/catch_test_macros.hpp>

#include <format>

#include "ServerFixture.hpp"

namespace idhan::test
{

TEST_CASE_METHOD( ServerFixture, "Unknown endpoint parameters warn without rejecting requests", "[api][parameters]" )
{
	const auto health { api().get( "/health", { { "query_typo_for_warning", "QUERY_VALUE_MUST_NOT_BE_LOGGED" } } ) };
	REQUIRE( health.status == 200 );
	CHECK( health.json[ "status" ].asString() == "ok" );
	Json::Value body { Json::objectValue };
	body[ "body_typo_for_warning" ] = "JSON_VALUE_MUST_NOT_BE_LOGGED";
	body[ "sort" ][ "by" ] = "filesize";
	body[ "sort" ][ "nested_typo_for_warning" ] = "NESTED_VALUE_MUST_NOT_BE_LOGGED";
	const auto search { api().post( "/search", body ) };
	REQUIRE( search.status == 200 );
	const auto log { api().get( "/log", { { "level", "warning" } } ) };
	REQUIRE( log.status == 200 );
	CHECK( log.body.find( "query_typo_for_warning" ) != std::string::npos );
	CHECK( log.body.find( "/body_typo_for_warning" ) != std::string::npos );
	CHECK( log.body.find( "/sort/nested_typo_for_warning" ) != std::string::npos );
	CHECK( log.body.find( "QUERY_VALUE_MUST_NOT_BE_LOGGED" ) == std::string::npos );
	CHECK( log.body.find( "JSON_VALUE_MUST_NOT_BE_LOGGED" ) == std::string::npos );
	CHECK( log.body.find( "NESTED_VALUE_MUST_NOT_BE_LOGGED" ) == std::string::npos );
}

TEST_CASE_METHOD( ServerFixture, "Declared names and free-form request data remain accepted", "[api][parameters]" )
{
	const auto record_id { api().createRecord( 2301 ) };
	const auto info { api().get( std::format( "/records/{}/info", record_id ), { { "idhan_key", api().key() } } ) };
	REQUIRE( info.status == 200 );
	CHECK( info.json[ "record_id" ].asInt() == record_id );

	Json::Value secrets { Json::objectValue };
	secrets[ "arbitrary_secret_name" ] = "secret_payload_is_not_a_parameter";
	CHECK( api().post( "/downloader/secrets", secrets ).status == 200 );
	const auto log { api().get( "/log", { { "level", "warning" } } ) };
	REQUIRE( log.status == 200 );
	CHECK( log.body.find( "arbitrary_secret_name" ) == std::string::npos );
	CHECK( log.body.find( "secret_payload_is_not_a_parameter" ) == std::string::npos );
}

} // namespace idhan::test
