#include <catch2/catch_test_macros.hpp>

#include "ServerFixture.hpp"

namespace idhan::test
{

SCENARIO_METHOD( ServerFixture, "Tag aliases", "[api][tags][aliases]" )
{
	GIVEN( "a domain and two tags" )
	{
		const auto domain { api().createDomain( "characters" ) };
		const auto samus { api().createTag( "character", "samus" ) };
		const auto samus_aran { api().createTag( "character", "samus aran" ) };

		WHEN( "one is aliased onto the other" )
		{
			const auto created { api().createAliases( domain, { { samus, samus_aran } } ) };

			THEN( "the request succeeds" )
			{
				CHECK( created.status == 200 );
			}

			THEN( "the alias row exists in that domain" )
			{
				pqxx::nontransaction tx { db() };

				CHECK(
					tx.query_value< int >(
						"SELECT count(*) FROM tag_aliases WHERE tag_domain_id = $1 AND aliased_id = $2 AND alias_id = $3",
						pqxx::params { domain, samus, samus_aran } )
				    == 1 );
			}

			AND_WHEN( "the same alias is created again" )
			{
				const auto again { api().createAliases( domain, { { samus, samus_aran } } ) };

				THEN( "the request succeeds and nothing is duplicated" )
				{
					CHECK( again.status == 200 );

					pqxx::nontransaction tx { db() };
					CHECK( tx.query_value< int >( "SELECT count(*) FROM tag_aliases" ) == 1 );
				}
			}

			// The insert conflicts on (tag_domain_id, aliased_id) and does nothing, so a tag cannot be
			// pointed somewhere else without removing its alias first, and the caller is not told.
			AND_WHEN( "the same tag is aliased onto a third tag" )
			{
				const auto ridley { api().createTag( "character", "ridley" ) };
				const auto rerouted { api().createAliases( domain, { { samus, ridley } } ) };

				THEN( "the request succeeds but the alias still points at the first target" )
				{
					CHECK( rerouted.status == 200 );

					pqxx::nontransaction tx { db() };
					CHECK(
						tx.query_value< int >(
							"SELECT alias_id FROM tag_aliases WHERE tag_domain_id = $1 AND aliased_id = $2",
							pqxx::params { domain, samus } )
					    == samus_aran );
				}
			}

			AND_WHEN( "the alias is removed" )
			{
				const auto removed { api().removeAliases( domain, { { samus, samus_aran } } ) };

				THEN( "the row goes" )
				{
					CHECK( removed.status == 200 );

					pqxx::nontransaction tx { db() };
					CHECK( tx.query_value< int >( "SELECT count(*) FROM tag_aliases" ) == 0 );
				}
			}
		}

		WHEN( "an alias that was never made is removed" )
		{
			const auto removed { api().removeAliases( domain, { { samus, samus_aran } } ) };

			THEN( "the request still succeeds" )
			{
				CHECK( removed.status == 200 );
			}
		}

		WHEN( "a tag is aliased onto itself" )
		{
			const auto created { api().createAliases( domain, { { samus, samus } } ) };

			THEN( "the request is rejected" )
			{
				CHECK( created.status == 400 );
			}
		}

		WHEN( "an empty array is posted" )
		{
			const auto created { api().createAliases( domain, {} ) };

			THEN( "the request succeeds and changes nothing" )
			{
				CHECK( created.status == 200 );

				pqxx::nontransaction tx { db() };
				CHECK( tx.query_value< int >( "SELECT count(*) FROM tag_aliases" ) == 0 );
			}
		}

		WHEN( "the root of the body is an object" )
		{
			const auto created { api().post(
				"/tags/alias/create", domainBody( "characters" ), { { "tag_domain_id", std::to_string( domain ) } } ) };

			THEN( "the request is rejected" )
			{
				CHECK( created.status == 400 );
			}
		}

		WHEN( "an alias id is not a number" )
		{
			Json::Value entry {};
			entry[ "aliased_id" ] = samus;
			entry[ "alias_id" ] = "samus aran";

			Json::Value body { Json::arrayValue };
			body.append( entry );

			const auto created {
				api().post( "/tags/alias/create", body, { { "tag_domain_id", std::to_string( domain ) } } )
			};

			THEN( "the request is rejected" )
			{
				CHECK( created.status == 400 );
			}
		}

		WHEN( "a tag that does not exist is aliased" )
		{
			const auto created { api().createAliases( domain, { { samus, 9999 } } ) };

			THEN( "the tag is not found" )
			{
				CHECK( created.status == 404 );
			}
		}

		WHEN( "the domain does not exist" )
		{
			const auto created { api().createAliases( 9999, { { samus, samus_aran } } ) };

			THEN( "the domain is not found" )
			{
				CHECK( created.status == 404 );
			}
		}

		WHEN( "no domain is given" )
		{
			const auto created { api().post( "/tags/alias/create", aliasBody( { { samus, samus_aran } } ) ) };

			THEN( "the request is rejected" )
			{
				CHECK( created.status == 400 );
			}
		}

		WHEN( "the domain is zero" )
		{
			const auto created { api().createAliases( 0, { { samus, samus_aran } } ) };

			THEN( "the request is rejected" )
			{
				CHECK( created.status == 400 );
			}
		}

		WHEN( "the domain is not a number" )
		{
			const auto created { api().post(
				"/tags/alias/create", aliasBody( { { samus, samus_aran } } ), { { "tag_domain_id", "sideways" } } ) };

			THEN( "the request is rejected" )
			{
				CHECK( created.status == 400 );
			}
		}
	}

	GIVEN( "an alias chain that would close on itself" )
	{
		const auto domain { api().createDomain( "characters" ) };
		const auto first { api().createTag( "character", "samus" ) };
		const auto second { api().createTag( "character", "samus aran" ) };

		REQUIRE( api().createAliases( domain, { { first, second } } ).status == 200 );

		WHEN( "the far end is aliased back to the near end" )
		{
			const auto created { api().createAliases( domain, { { second, first } } ) };

			THEN( "the cycle is refused" )
			{
				CHECK( created.status == 409 );
			}
		}
	}
}

TEST_CASE_METHOD(
	ServerFixture,
	"Alias batches roll back completely when a later pair creates a cycle",
	"[api][tags][aliases][atomic]" )
{
	const auto domain { api().createDomain( "atomic aliases" ) };
	const auto a { api().createTag( "", "a" ) };
	const auto b { api().createTag( "", "b" ) };
	const auto c { api().createTag( "", "c" ) };
	CHECK( api().createAliases( domain, { { a, b }, { b, c }, { c, a } } ).status == 409 );
	pqxx::nontransaction read { db() };
	CHECK(
		read.query_value< int >( "SELECT COUNT(*) FROM tag_aliases WHERE tag_domain_id = $1", pqxx::params { domain } )
	    == 0 );
	CHECK( api().createAliases( domain, { { a, b }, { b, c } } ).status == 200 );
	CHECK(
		read.query_value< int >(
			"SELECT effective_tag_id FROM tag_aliases "
			"WHERE tag_domain_id = $1 AND aliased_id = $2",
			pqxx::params { domain, a } )
	    == c );
}

TEST_CASE_METHOD(
	ServerFixture,
	"Malformed alias removal batches preserve all existing relationships",
	"[api][tags][aliases][atomic]" )
{
	const auto domain { api().createDomain( "atomic removal" ) };
	const auto a { api().createTag( "", "a" ) };
	const auto b { api().createTag( "", "b" ) };
	const auto c { api().createTag( "", "c" ) };
	REQUIRE( api().createAliases( domain, { { a, b }, { b, c } } ).status == 200 );
	auto body { aliasBody( { { a, b }, { b, c } } ) };
	body[ 1 ][ "aliased_id" ] = "invalid";
	CHECK( api().post( "/tags/alias/remove", body, { { "tag_domain_id", std::to_string( domain ) } } ).status == 400 );
	pqxx::nontransaction read { db() };
	CHECK(
		read.query_value< int >( "SELECT COUNT(*) FROM tag_aliases WHERE tag_domain_id = $1", pqxx::params { domain } )
	    == 2 );
}

} // namespace idhan::test
