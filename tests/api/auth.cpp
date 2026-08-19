#include <catch2/catch_test_macros.hpp>

#include "ServerFixture.hpp"

namespace idhan::test
{

//! Well formed, 64 hex characters, and not the key the server minted.
constexpr std::string_view UNKNOWN_KEY { "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" };

SCENARIO_METHOD( ServerFixture, "API key checks", "[api][auth]" )
{
	if ( authDisabled() ) SKIP( "the server was built with IDHAN_DISABLE_API_AUTH" );

	GIVEN( "an endpoint behind the auth filter" )
	{
		WHEN( "a request carries the key" )
		{
			const auto listed { api().get( "/tags/domain/list" ) };

			THEN( "it is answered" )
			{
				CHECK( listed.status == drogon::k200OK );
			}
		}

		WHEN( "a request carries no key" )
		{
			const auto listed { api().getWithKey( "/tags/domain/list", {} ) };

			THEN( "it is rejected as a bad request" )
			{
				CHECK( listed.status == drogon::k400BadRequest );
			}
		}

		WHEN( "a request carries a key that is not 64 characters" )
		{
			const auto listed { api().getWithKey( "/tags/domain/list", "abcdef" ) };

			THEN( "it is rejected as a bad request" )
			{
				CHECK( listed.status == drogon::k400BadRequest );
			}
		}

		WHEN( "a request carries a well formed key the server does not know" )
		{
			const auto listed { api().getWithKey( "/tags/domain/list", std::string( UNKNOWN_KEY ) ) };

			THEN( "it is unauthorized" )
			{
				CHECK( listed.status == drogon::k401Unauthorized );
			}
		}
	}

	GIVEN( "a schema that already minted its key" )
	{
		WHEN( "another key is asked for" )
		{
			const auto minted { api().get( "/generate_api_key" ) };

			THEN( "the request conflicts" )
			{
				CHECK( minted.status == drogon::k409Conflict );
			}
		}
	}
}

} // namespace idhan::test
