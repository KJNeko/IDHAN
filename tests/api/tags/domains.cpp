#include <catch2/catch_test_macros.hpp>

#include "ServerFixture.hpp"

namespace idhan::test
{

SCENARIO_METHOD( ServerFixture, "Tag domains", "[api][tags][domains]" )
{
	GIVEN( "only the seeded default domain" )
	{
		WHEN( "a domain is created" )
		{
			const auto created { api().post( "/tags/domain/create", domainBody( "characters" ) ) };

			THEN( "the response names the new domain" )
			{
				REQUIRE( created.status == drogon::k200OK );
				CHECK( created.json[ "domain_name" ].asString() == "characters" );
				CHECK( domainOf( created ) > 0 );
			}

			THEN( "its partitions exist" )
			{
				CHECK( partitionExists( db(), "tag_mappings_domain_", domainOf( created ) ) );
				CHECK( partitionExists( db(), "tag_aliases_domain_", domainOf( created ) ) );
				CHECK( partitionExists( db(), "tag_parents_domain_", domainOf( created ) ) );
			}

			THEN( "it is listed alongside the default" )
			{
				const auto listed { api().get( "/tags/domain/list" ) };

				REQUIRE( listed.status == drogon::k200OK );
				REQUIRE( listed.json.isArray() );
				CHECK( listed.json.size() == 2 );
			}

			THEN( "its info can be read back" )
			{
				const auto info { api().get( "/tags/domain/" + std::to_string( domainOf( created ) ) + "/info" ) };

				REQUIRE( info.status == drogon::k200OK );
				CHECK( info.json[ "domain_name" ].asString() == "characters" );
				CHECK( domainOf( info ) == domainOf( created ) );
			}

			AND_WHEN( "the same name is created again" )
			{
				const auto again { api().post( "/tags/domain/create", domainBody( "characters" ) ) };

				THEN( "it conflicts and returns the existing domain" )
				{
					REQUIRE( again.status == drogon::k409Conflict );
					CHECK( domainOf( again ) == domainOf( created ) );
				}
			}

			AND_WHEN( "it is deleted" )
			{
				const auto deleted { api().del( "/tags/domain/" + std::to_string( domainOf( created ) ) + "/delete" ) };

				THEN( "the response names the domain that went" )
				{
					REQUIRE( deleted.status == drogon::k200OK );
					CHECK( domainOf( deleted ) == domainOf( created ) );
				}

				THEN( "deleting it a second time finds nothing" )
				{
					const auto twice {
						api().del( "/tags/domain/" + std::to_string( domainOf( created ) ) + "/delete" )
					};

					CHECK( twice.status == drogon::k404NotFound );
				}
			}
		}

		WHEN( "the domain list is read" )
		{
			const auto listed { api().get( "/tags/domain/list" ) };

			THEN( "it holds the default domain alone" )
			{
				REQUIRE( listed.status == drogon::k200OK );
				REQUIRE( listed.json.isArray() );
				REQUIRE( listed.json.size() == 1 );
				CHECK( listed.json[ 0 ][ "domain_name" ].asString() == "default" );
			}
		}

		WHEN( "info is asked for a domain that does not exist" )
		{
			const auto info { api().get( "/tags/domain/9999/info" ) };

			THEN( "it is not found" )
			{
				CHECK( info.status == drogon::k404NotFound );
			}
		}

		WHEN( "a domain is created without a name" )
		{
			Json::Value body {};
			body[ "not_a_name" ] = "characters";

			const auto created { api().post( "/tags/domain/create", body ) };

			THEN( "the request is rejected" )
			{
				CHECK( created.status == drogon::k400BadRequest );
			}
		}

		WHEN( "a domain is created from a json array" )
		{
			const auto created { api().post( "/tags/domain/create", Json::Value { Json::arrayValue } ) };

			THEN( "the request is rejected" )
			{
				CHECK( created.status == drogon::k400BadRequest );
			}
		}
	}

	GIVEN( "a domain holding an alias" )
	{
		const auto domain { api().createDomain( "characters" ) };
		const auto samus { api().createTag( "character", "samus aran" ) };
		const auto samus_aran { api().createTag( "character", "samus" ) };

		REQUIRE( api().createAliases( domain, { { samus, samus_aran } } ).status == drogon::k200OK );

		WHEN( "the domain is deleted" )
		{
			REQUIRE( api().del( "/tags/domain/" + std::to_string( domain ) + "/delete" ).status == drogon::k200OK );

			THEN( "its alias rows go with it" )
			{
				pqxx::nontransaction tx { db() };
				CHECK( tx.query_value< int >( "SELECT count(*) FROM tag_aliases" ) == 0 );
			}

			THEN( "its partitions remain" )
			{
				CHECK( partitionExists( db(), "tag_aliases_domain_", domain ) );
			}
		}
	}
}

} // namespace idhan::test
