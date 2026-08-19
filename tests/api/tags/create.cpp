#include <catch2/catch_test_macros.hpp>

#include "ServerFixture.hpp"

namespace idhan::test
{

SCENARIO_METHOD( ServerFixture, "Tag creation", "[api][tags][create]" )
{
	GIVEN( "no tags" )
	{
		WHEN( "a batch of distinct pairs is posted" )
		{
			const auto created { api().post(
				"/tags/create",
				tagBody( { { "character", "samus aran" }, { "", "blonde hair" }, { "series", "metroid" } } ) ) };

			THEN( "one id comes back per pair, in order" )
			{
				REQUIRE( created.status == drogon::k200OK );
				REQUIRE( created.json.isArray() );
				REQUIRE( created.json.size() == 3 );

				CHECK( created.json[ 0 ][ "tag_id" ].asInt() > 0 );
				CHECK( created.json[ 1 ][ "tag_id" ].asInt() > 0 );
				CHECK( created.json[ 2 ][ "tag_id" ].asInt() > 0 );
			}

			THEN( "the tags are in the database" )
			{
				pqxx::nontransaction tx { db() };

				CHECK( tx.query_value< int >( "SELECT count(*) FROM tags" ) == 3 );
				CHECK(
					tx.query_value< std::string >(
						"SELECT tag_text FROM tags WHERE tag_id = $1",
						pqxx::params { created.json[ 0 ][ "tag_id" ].asInt() } )
					== "character:samus aran" );
			}

			AND_WHEN( "the same batch is posted again" )
			{
				const auto again { api().post(
					"/tags/create",
					tagBody( { { "character", "samus aran" }, { "", "blonde hair" }, { "series", "metroid" } } ) ) };

				THEN( "it returns the tags the first post created" )
				{
					REQUIRE( again.status == drogon::k200OK );
					CHECK( again.json == created.json );

					pqxx::nontransaction tx { db() };
					CHECK( tx.query_value< int >( "SELECT count(*) FROM tags" ) == 3 );
				}
			}
		}

		WHEN( "a pair is posted twice in one batch" )
		{
			const auto created { api().post(
				"/tags/create", tagBody( { { "character", "samus aran" }, { "character", "samus aran" } } ) ) };

			THEN( "both entries name the same tag" )
			{
				REQUIRE( created.status == drogon::k200OK );
				REQUIRE( created.json.size() == 2 );
				CHECK( created.json[ 0 ][ "tag_id" ] == created.json[ 1 ][ "tag_id" ] );
			}
		}

		WHEN( "an empty array is posted" )
		{
			const auto created { api().post( "/tags/create", Json::Value { Json::arrayValue } ) };

			THEN( "the request is rejected" )
			{
				CHECK( created.status == drogon::k400BadRequest );
			}
		}

		WHEN( "the root of the body is an object" )
		{
			const auto created { api().post( "/tags/create", domainBody( "characters" ) ) };

			THEN( "the request is rejected" )
			{
				CHECK( created.status == drogon::k400BadRequest );
			}
		}

		WHEN( "a subtag is not a string" )
		{
			Json::Value entry {};
			entry[ "namespace" ] = "character";
			entry[ "subtag" ] = 7;

			Json::Value body { Json::arrayValue };
			body.append( entry );

			const auto created { api().post( "/tags/create", body ) };

			THEN( "the request is rejected" )
			{
				CHECK( created.status == drogon::k400BadRequest );
			}
		}

		WHEN( "a namespace key is missing" )
		{
			Json::Value entry {};
			entry[ "subtag" ] = "samus aran";

			Json::Value body { Json::arrayValue };
			body.append( entry );

			const auto created { api().post( "/tags/create", body ) };

			THEN( "the request is rejected" )
			{
				CHECK( created.status == drogon::k400BadRequest );
			}
		}
	}

	GIVEN( "a tag that already exists" )
	{
		const auto existing { api().createTag( "Character", "Samus Aran" ) };

		WHEN( "the same tag is posted in a different case" )
		{
			const auto created { api().post( "/tags/create", tagBody( { { "CHARACTER", "SAMUS ARAN" } } ) ) };

			THEN( "it folds onto the tag that exists" )
			{
				REQUIRE( created.status == drogon::k200OK );
				REQUIRE( created.json.size() == 1 );
				CHECK( created.json[ 0 ][ "tag_id" ].asInt() == existing );

				pqxx::nontransaction tx { db() };
				CHECK( tx.query_value< int >( "SELECT count(*) FROM tags" ) == 1 );
			}
		}
	}
}

} // namespace idhan::test
