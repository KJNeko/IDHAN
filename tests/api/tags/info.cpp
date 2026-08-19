#include <catch2/catch_test_macros.hpp>

#include "ServerFixture.hpp"

namespace idhan::test
{

SCENARIO_METHOD( ServerFixture, "Tag info", "[api][tags][info]" )
{
	GIVEN( "a tag" )
	{
		const auto samus { api().createTag( "Character", "Samus Aran" ) };

		WHEN( "its info is read by path" )
		{
			const auto info { api().get( "/tags/" + std::to_string( samus ) + "/info" ) };

			THEN( "it carries the folded namespace and subtag" )
			{
				REQUIRE( info.status == drogon::k200OK );
				CHECK( info.json[ "tag_id" ].asInt() == samus );
				CHECK( info.json[ "namespace" ][ "text" ].asString() == "character" );
				CHECK( info.json[ "subtag" ][ "text" ].asString() == "samus aran" );
			}

			THEN( "it is used by nothing" )
			{
				CHECK( info.json[ "items_count" ].asInt() == 0 );
			}
		}

		// /tags/info carries two registrations, tag_id and tag_ids, on one path. The later one wins, so the
		// tag_id form binds nothing and the handler is handed a zero.
		WHEN( "its info is read through the tag_id query form" )
		{
			const auto info { api().get( "/tags/info", { { "tag_id", std::to_string( samus ) } } ) };

			THEN( "the id never reaches the handler" )
			{
				CHECK( info.status == drogon::k400BadRequest );
			}
		}

		WHEN( "its info is read through the tag_ids query form" )
		{
			const auto by_path { api().get( "/tags/" + std::to_string( samus ) + "/info" ) };
			const auto by_query { api().get( "/tags/info", { { "tag_ids", std::to_string( samus ) } } ) };

			THEN( "it answers the same as the path route" )
			{
				REQUIRE( by_query.status == drogon::k200OK );
				CHECK( by_query.json == by_path.json );
			}
		}

		// The plural route binds a single TagID, so only the first id survives the conversion.
		WHEN( "several ids are passed to the plural route" )
		{
			const auto info { api().get( "/tags/info", { { "tag_ids", std::to_string( samus ) + ",1" } } ) };

			THEN( "only the first id is answered for, and the rest are dropped silently" )
			{
				REQUIRE( info.status == drogon::k200OK );
				CHECK( info.json[ "tag_id" ].asInt() == samus );
				CHECK( info.json[ "subtag" ][ "text" ].asString() == "samus aran" );
			}
		}
	}

	GIVEN( "no tags" )
	{
		WHEN( "info is asked for an id that does not exist" )
		{
			const auto info { api().get( "/tags/9999/info" ) };

			// The domain endpoints answer 404 for the same mistake.
			THEN( "the answer is a bad request rather than a not found" )
			{
				CHECK( info.status == drogon::k400BadRequest );
			}
		}
	}
}

} // namespace idhan::test
