#include <catch2/catch_test_macros.hpp>

#include "ServerFixture.hpp"

namespace idhan::test
{

TEST_CASE_METHOD(
	ServerFixture,
	"Downloader debug accepts named event cursors and the legacy alias",
	"[api][debug-cursors]" )
{
	CHECK( api().get( "/downloader/debug" ).status == 200 );
	for ( const auto* name : { "event_cursors", "since" } )
	{
		for ( const auto* value : { "", "12:100", "12:100,15:40", "12:0", "12:100,12:200", "12:100," } )
		{
			CAPTURE( name, value );
			const auto response { api().get( "/downloader/debug", { { name, value } } ) };
			REQUIRE( response.status == 200 );
			CHECK( response.json[ "sessions" ].isArray() );
			CHECK( response.json[ "lanes" ].isArray() );
		}
	}
	const auto log { api().get( "/log", { { "level", "warning" } } ) };
	REQUIRE( log.status == 200 );
	CHECK( log.body.find( "Unknown query parameters: \"event_cursors\"" ) == std::string::npos );
	CHECK( log.body.find( "Unknown query parameters: \"since\"" ) == std::string::npos );
}

TEST_CASE_METHOD( ServerFixture, "Downloader debug rejects malformed event cursor values", "[api][debug-cursors]" )
{
	for ( const auto* name : { "event_cursors", "since" } )
	{
		for ( const auto* value :
		      { "100",
		        "12:",
		        ":100",
		        "12:100:200",
		        "12:-1",
		        "12: 100",
		        " 12:100",
		        "12:100,,15:40",
		        "9223372036854775808:1",
		        "12:18446744073709551616" } )
		{
			CAPTURE( name, value );
			const auto response { api().get( "/downloader/debug", { { name, value } } ) };
			CHECK( response.status == 400 );
			CHECK( response.body.find( name ) != std::string::npos );
			CHECK( response.body.find( "<session_id>:<event_sequence>" ) != std::string::npos );
		}
	}
}

TEST_CASE_METHOD( ServerFixture, "Downloader debug rejects ambiguous cursor parameters", "[api][debug-cursors]" )
{
	for ( const auto& query :
	      { QueryParams { { "event_cursors", "12:100" }, { "since", "12:100" } },
	        QueryParams { { "event_cursors", "" }, { "since", "" } },
	        QueryParams { { "event_cursors", "12:100" }, { "event_cursors", "15:40" } },
	        QueryParams { { "since", "12:100" }, { "since", "15:40" } } } )
	{
		CHECK( api().get( "/downloader/debug", query ).status == 400 );
	}
}

} // namespace idhan::test
