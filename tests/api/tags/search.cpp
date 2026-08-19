#include <catch2/catch_test_macros.hpp>

#include "ServerFixture.hpp"

namespace idhan::test
{

//! "Amelie" carrying a precomposed U+00E9, and the same name spelled with a combining acute.
constexpr std::string_view AMELIE_PRECOMPOSED {
	"Am"
	"\xc3\xa9"
	"lie"
};

constexpr std::string_view AMELIE_DECOMPOSED {
	"Ame"
	"\xcc\x81"
	"lie"
};

SCENARIO_METHOD( ServerFixture, "Tag search", "[api][tags][search]" )
{
	GIVEN( "a tag created from mixed case parts" )
	{
		const auto samus { api().createTag( "Character", "Samus Aran" ) };

		WHEN( "its folded text is searched for" )
		{
			const auto found { api().get( "/tags/search", { { "tag", "character:samus aran" } } ) };

			THEN( "the tag is found" )
			{
				REQUIRE( found.status == drogon::k200OK );
				CHECK( found.json[ "found" ].asBool() );
				CHECK( found.json[ "tag_id" ].asInt() == samus );
			}
		}

		WHEN( "it is searched for in a different case" )
		{
			const auto found { api().get( "/tags/search", { { "tag", "CHARACTER:SAMUS ARAN" } } ) };

			THEN( "the tag is found" )
			{
				REQUIRE( found.status == drogon::k200OK );
				CHECK( found.json[ "found" ].asBool() );
				CHECK( found.json[ "tag_id" ].asInt() == samus );
			}
		}

		WHEN( "a tag that does not exist is searched for" )
		{
			const auto found { api().get( "/tags/search", { { "tag", "character:ridley" } } ) };

			THEN( "nothing is found, and no id is offered" )
			{
				REQUIRE( found.status == drogon::k200OK );
				CHECK_FALSE( found.json[ "found" ].asBool() );
				CHECK_FALSE( found.json.isMember( "tag_id" ) );
			}
		}
	}

	GIVEN( "a tag holding a precomposed accent" )
	{
		const auto amelie { api().createTag( "character", std::string( AMELIE_PRECOMPOSED ) ) };

		WHEN( "it is searched for by its decomposed spelling" )
		{
			const auto found {
				api().get( "/tags/search", { { "tag", "character:" + std::string( AMELIE_DECOMPOSED ) } } )
			};

			THEN( "the tag is found" )
			{
				REQUIRE( found.status == drogon::k200OK );
				CHECK( found.json[ "found" ].asBool() );
				CHECK( found.json[ "tag_id" ].asInt() == amelie );
			}
		}
	}
}

} // namespace idhan::test
