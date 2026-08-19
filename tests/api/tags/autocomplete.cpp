#include <catch2/catch_test_macros.hpp>

#include "ServerFixture.hpp"

namespace idhan::test
{

SCENARIO_METHOD( ServerFixture, "Tag autocomplete", "[api][tags][autocomplete]" )
{
	GIVEN( "no tags" )
	{
		WHEN( "anything is autocompleted" )
		{
			const auto completed { api().get( "/tags/autocomplete", { { "tag", "samus" } } ) };

			THEN( "the answer is an empty array rather than null" )
			{
				REQUIRE( completed.status == drogon::k200OK );
				REQUIRE( completed.json.isArray() );
				CHECK( completed.json.empty() );
			}
		}
	}

	GIVEN( "a handful of tags sharing a prefix" )
	{
		api().createTags( { { "character", "samus aran" }, { "character", "samus" }, { "series", "metroid" } } );

		WHEN( "the prefix is autocompleted" )
		{
			const auto completed { api().get( "/tags/autocomplete", { { "tag", "samus" } } ) };

			THEN( "both matching tags come back and the unrelated one does not" )
			{
				REQUIRE( completed.status == drogon::k200OK );
				REQUIRE( completed.json.size() == 2 );

				for ( const auto& tag : completed.json ) CHECK( tag[ "tag_text" ].asString().contains( "samus" ) );
			}

			THEN( "every entry carries the fields a client sorts on" )
			{
				for ( const auto& tag : completed.json )
				{
					CHECK( tag.isMember( "tag_id" ) );
					CHECK( tag.isMember( "tag_text" ) );
					CHECK( tag.isMember( "similarity" ) );
					CHECK( tag.isMember( "count" ) );
				}
			}
		}

		WHEN( "an exact tag text is autocompleted" )
		{
			const auto completed { api().get( "/tags/autocomplete", { { "tag", "character:samus" } } ) };

			THEN( "the exact match sorts first" )
			{
				REQUIRE( completed.status == drogon::k200OK );
				REQUIRE( completed.json.size() >= 1 );
				CHECK( completed.json[ 0 ][ "tag_text" ].asString() == "character:samus" );
			}
		}

		WHEN( "the limit is one" )
		{
			const auto completed { api().get( "/tags/autocomplete", { { "tag", "samus" }, { "limit", "1" } } ) };

			THEN( "one tag comes back" )
			{
				REQUIRE( completed.status == drogon::k200OK );
				CHECK( completed.json.size() == 1 );
			}
		}

		WHEN( "unused tags are excluded" )
		{
			const auto completed {
				api().get( "/tags/autocomplete", { { "tag", "samus" }, { "include_unused", "false" } } )
			};

			THEN( "nothing comes back, since none of them are on a record" )
			{
				REQUIRE( completed.status == drogon::k200OK );
				CHECK( completed.json.empty() );
			}
		}

		WHEN( "the search is negated with a leading dash" )
		{
			const auto completed { api().get( "/tags/autocomplete", { { "tag", "-samus" } } ) };

			THEN( "the dash is stripped before matching" )
			{
				REQUIRE( completed.status == drogon::k200OK );
				CHECK( completed.json.size() == 2 );
			}
		}

		WHEN( "an unknown display type is asked for" )
		{
			const auto completed {
				api().get( "/tags/autocomplete", { { "tag", "samus" }, { "tag_display_type", "sideways" } } )
			};

			THEN( "the request is rejected" )
			{
				CHECK( completed.status == drogon::k400BadRequest );
			}
		}

		// The parameter is validated but never used, so both accepted values answer alike.
		WHEN( "storage and display are both asked for" )
		{
			const auto storage {
				api().get( "/tags/autocomplete", { { "tag", "samus" }, { "tag_display_type", "storage" } } )
			};
			const auto display {
				api().get( "/tags/autocomplete", { { "tag", "samus" }, { "tag_display_type", "display" } } )
			};

			THEN( "they answer the same" )
			{
				REQUIRE( storage.status == drogon::k200OK );
				REQUIRE( display.status == drogon::k200OK );
				CHECK( storage.json == display.json );
			}
		}
	}
}

} // namespace idhan::test
