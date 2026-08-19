#include <catch2/catch_test_macros.hpp>

#include "ServerFixture.hpp"

namespace idhan::test
{

SCENARIO_METHOD( ServerFixture, "Tag siblings", "[api][tags][siblings]" )
{
	SKIP();

	GIVEN( "a domain and two tags" )
	{
		const auto domain { api().createDomain( "characters" ) };
		const auto samus { api().createTag( "character", "samus" ) };
		const auto samus_aran { api().createTag( "character", "samus aran" ) };

		const QueryParams domain_query { { "tag_domain_id", std::to_string( domain ) } };

		WHEN( "siblings are created" )
		{
			Json::Value entry {};
			entry[ "older_id" ] = samus;
			entry[ "younger_id" ] = samus_aran;

			Json::Value body { Json::arrayValue };
			body.append( entry );

			const auto created { api().post( "/tags/siblings/create", body, domain_query ) };

			THEN( "the endpoint reports itself unimplemented" )
			{
				CHECK( created.status == drogon::k501NotImplemented );
			}
		}

		WHEN( "siblings are removed" )
		{
			const auto removed {
				api().post( "/tags/siblings/remove", Json::Value { Json::arrayValue }, domain_query )
			};

			THEN( "the endpoint reports itself unimplemented" )
			{
				CHECK( removed.status == drogon::k501NotImplemented );
			}
		}
	}
}

} // namespace idhan::test
