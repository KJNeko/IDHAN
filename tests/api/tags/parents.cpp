#include <catch2/catch_test_macros.hpp>

#include "ServerFixture.hpp"

namespace idhan::test
{

SCENARIO_METHOD( ServerFixture, "Tag parents", "[api][tags][parents]" )
{
	GIVEN( "a domain and two tags" )
	{
		const auto domain { api().createDomain( "characters" ) };
		const auto metroid { api().createTag( "series", "metroid" ) };
		const auto samus { api().createTag( "character", "samus aran" ) };

		WHEN( "one is made the parent of the other" )
		{
			const auto created { api().createParents( domain, { { metroid, samus } } ) };

			THEN( "the request succeeds" )
			{
				CHECK( created.status == 200 );
			}

			THEN( "the parent row exists in that domain" )
			{
				pqxx::nontransaction tx { db() };

				CHECK(
					tx.query_value< int >(
						"SELECT count(*) FROM tag_parents WHERE tag_domain_id = $1 AND parent_id = $2 AND child_id = $3",
						pqxx::params { domain, metroid, samus } )
				    == 1 );
			}

			AND_WHEN( "the same pair is created again" )
			{
				const auto again { api().createParents( domain, { { metroid, samus } } ) };

				THEN( "the request succeeds and nothing is duplicated" )
				{
					CHECK( again.status == 200 );

					pqxx::nontransaction tx { db() };
					CHECK( tx.query_value< int >( "SELECT count(*) FROM tag_parents" ) == 1 );
				}
			}

			AND_WHEN( "the child is made the parent of its own parent" )
			{
				const auto created_cycle { api().createParents( domain, { { samus, metroid } } ) };

				THEN( "the cycle is refused" )
				{
					CHECK( created_cycle.status == 409 );
				}
			}

			AND_WHEN( "the pair is removed" )
			{
				const auto removed { api().removeParents( domain, { { metroid, samus } } ) };

				THEN( "the row goes" )
				{
					CHECK( removed.status == 200 );

					pqxx::nontransaction tx { db() };
					CHECK( tx.query_value< int >( "SELECT count(*) FROM tag_parents" ) == 0 );
				}
			}
		}

		WHEN( "a pair that was never made is removed" )
		{
			const auto removed { api().removeParents( domain, { { metroid, samus } } ) };

			THEN( "the request still succeeds" )
			{
				CHECK( removed.status == 200 );
			}
		}

		WHEN( "a tag is made its own parent" )
		{
			const auto created { api().createParents( domain, { { samus, samus } } ) };

			THEN( "the request is rejected" )
			{
				CHECK( created.status == 400 );
			}
		}

		WHEN( "a tag that does not exist is named" )
		{
			const auto created { api().createParents( domain, { { metroid, 9999 } } ) };

			THEN( "the tag is not found" )
			{
				CHECK( created.status == 404 );
			}
		}

		WHEN( "the domain does not exist" )
		{
			const auto created { api().createParents( 9999, { { metroid, samus } } ) };

			THEN( "the domain is not found" )
			{
				CHECK( created.status == 404 );
			}
		}

		WHEN( "no domain is given" )
		{
			const auto created { api().post( "/tags/parents/create", parentBody( { { metroid, samus } } ) ) };

			THEN( "the request is rejected" )
			{
				CHECK( created.status == 400 );
			}
		}

		WHEN( "the domain is zero" )
		{
			const auto created { api().createParents( 0, { { metroid, samus } } ) };

			THEN( "the request is rejected" )
			{
				CHECK( created.status == 400 );
			}
		}

		WHEN( "the root of the body is an object" )
		{
			const auto created { api().post(
				"/tags/parents/create",
				domainBody( "characters" ),
				{ { "tag_domain_id", std::to_string( domain ) } } ) };

			THEN( "the request is rejected" )
			{
				CHECK( created.status == 400 );
			}
		}

		WHEN( "a parent id is not a number" )
		{
			Json::Value entry {};
			entry[ "parent_id" ] = "metroid";
			entry[ "child_id" ] = samus;

			Json::Value body { Json::arrayValue };
			body.append( entry );

			const auto created {
				api().post( "/tags/parents/create", body, { { "tag_domain_id", std::to_string( domain ) } } )
			};

			THEN( "the request is rejected" )
			{
				CHECK( created.status == 400 );
			}
		}
	}
}

TEST_CASE_METHOD(
	ServerFixture,
	"Parent batches roll back completely when a later pair creates a cycle",
	"[api][tags][parents][atomic]" )
{
	const auto domain { api().createDomain( "atomic parents" ) };
	const auto a { api().createTag( "", "a" ) };
	const auto b { api().createTag( "", "b" ) };
	const auto c { api().createTag( "", "c" ) };
	CHECK( api().createParents( domain, { { a, b }, { b, c }, { c, a } } ).status == 409 );
	pqxx::nontransaction read { db() };
	CHECK(
		read.query_value< int >( "SELECT COUNT(*) FROM tag_parents WHERE tag_domain_id = $1", pqxx::params { domain } )
	    == 0 );
	CHECK( api().createParents( domain, { { a, b }, { b, c } } ).status == 200 );
}

} // namespace idhan::test
