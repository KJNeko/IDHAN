#include <catch2/catch_test_macros.hpp>

#include "ServerFixture.hpp"

namespace idhan::test
{

static std::string relationshipsPath( const TagDomainID tag_domain_id, const TagID tag_id )
{
	return "/tags/" + std::to_string( tag_domain_id ) + "/" + std::to_string( tag_id ) + "/relationships";
}

SCENARIO_METHOD( ServerFixture, "Tag relationships", "[api][tags][relationships]" )
{
	GIVEN( "a domain and two tags with nothing between them" )
	{
		const auto domain { api().createDomain( "characters" ) };
		const auto samus { api().createTag( "character", "samus" ) };
		const auto samus_aran { api().createTag( "character", "samus aran" ) };

		WHEN( "the relationships of one are read" )
		{
			const auto relationships { api().get( relationshipsPath( domain, samus ) ) };

			THEN( "every side is an empty array" )
			{
				REQUIRE( relationships.status == drogon::k200OK );

				for ( const auto& side :
				      { "parents", "children", "older_siblings", "younger_siblings", "aliases", "aliased" } )
				{
					REQUIRE( relationships.json[ side ].isArray() );
					CHECK( relationships.json[ side ].empty() );
				}
			}
		}

		WHEN( "one is aliased onto the other" )
		{
			REQUIRE( api().createAliases( domain, { { samus, samus_aran } } ).status == drogon::k200OK );

			THEN( "the aliased tag names its alias" )
			{
				const auto relationships { api().get( relationshipsPath( domain, samus ) ) };

				REQUIRE( relationships.status == drogon::k200OK );
				REQUIRE( relationships.json[ "aliases" ].size() == 1 );
				CHECK( relationships.json[ "aliases" ][ 0 ].asInt() == samus_aran );
				CHECK( relationships.json[ "aliased" ].empty() );
			}

			THEN( "the target names what points at it" )
			{
				const auto relationships { api().get( relationshipsPath( domain, samus_aran ) ) };

				REQUIRE( relationships.status == drogon::k200OK );
				REQUIRE( relationships.json[ "aliased" ].size() == 1 );
				CHECK( relationships.json[ "aliased" ][ 0 ].asInt() == samus );
				CHECK( relationships.json[ "aliases" ].empty() );
			}

			THEN( "another domain sees nothing" )
			{
				const auto other { api().createDomain( "other" ) };
				const auto relationships { api().get( relationshipsPath( other, samus ) ) };

				REQUIRE( relationships.status == drogon::k200OK );
				CHECK( relationships.json[ "aliases" ].empty() );
			}
		}

		WHEN( "one is made the parent of the other" )
		{
			REQUIRE( api().createParents( domain, { { samus, samus_aran } } ).status == drogon::k200OK );

			THEN( "the parent names its child" )
			{
				const auto relationships { api().get( relationshipsPath( domain, samus ) ) };

				REQUIRE( relationships.status == drogon::k200OK );
				REQUIRE( relationships.json[ "children" ].size() == 1 );
				CHECK( relationships.json[ "children" ][ 0 ].asInt() == samus_aran );
				CHECK( relationships.json[ "parents" ].empty() );
			}

			THEN( "the child names its parent" )
			{
				const auto relationships { api().get( relationshipsPath( domain, samus_aran ) ) };

				REQUIRE( relationships.status == drogon::k200OK );
				REQUIRE( relationships.json[ "parents" ].size() == 1 );
				CHECK( relationships.json[ "parents" ][ 0 ].asInt() == samus );
				CHECK( relationships.json[ "children" ].empty() );
			}

			THEN( "the sibling sides stay empty, since siblings are not resolved" )
			{
				const auto relationships { api().get( relationshipsPath( domain, samus ) ) };

				CHECK( relationships.json[ "older_siblings" ].empty() );
				CHECK( relationships.json[ "younger_siblings" ].empty() );
			}
		}
	}
}

} // namespace idhan::test
