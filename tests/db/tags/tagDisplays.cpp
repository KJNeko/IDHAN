#include <catch2/catch_test_macros.hpp>

#include "MigratedSchema.hpp"
#include "TagHelpers.hpp"

namespace idhan::test
{

//! How a reader is meant to resolve a tag's display form: the override if there is one, the folded text if not.
constexpr std::string_view RESOLVE_DISPLAY {
	"SELECT COALESCE(d.display_text, t.tag_text) FROM tags t "
	"LEFT JOIN tag_displays d USING (tag_id) WHERE t.tag_id = $1"
};

static void setDisplay( pqxx::transaction_base& tx, const int tag_id, const std::string_view display )
{
	tx.exec(
		"INSERT INTO tag_displays (tag_id, display_text) VALUES ($1, $2) "
		"ON CONFLICT (tag_id) DO UPDATE SET display_text = EXCLUDED.display_text",
		pqxx::params { tag_id, display } );
}

SCENARIO_METHOD( MigratedSchema, "Tag displays", "[db][tags][displays]" )
{
	pqxx::work tx { connection() };

	GIVEN( "a tag created from mixed case parts" )
	{
		const auto tag_id { insertTag( tx, "Character", "Samus Aran" ) };

		WHEN( "it carries no display row" )
		{
			THEN( "it resolves to its folded text" )
			{
				CHECK( tx.query_value< int >( "SELECT count(*) FROM tag_displays" ) == 0 );
				CHECK(
					tx.query_value< std::string >( RESOLVE_DISPLAY, pqxx::params { tag_id } )
					== "character:samus aran" );
			}
		}

		WHEN( "a display is set" )
		{
			setDisplay( tx, tag_id, "Character:Samus Aran" );

			THEN( "the display overrides the folded text without changing it" )
			{
				CHECK(
					tx.query_value< std::string >( RESOLVE_DISPLAY, pqxx::params { tag_id } )
					== "Character:Samus Aran" );
				CHECK( tx.query_value< std::string >( "SELECT tag_text FROM tags" ) == "character:samus aran" );
			}

			AND_WHEN( "the display is set again" )
			{
				setDisplay( tx, tag_id, "character:SAMUS ARAN" );

				THEN( "the tag still carries exactly one display row, holding the newer text" )
				{
					CHECK(
						tx.query_value< std::string >( RESOLVE_DISPLAY, pqxx::params { tag_id } )
						== "character:SAMUS ARAN" );
					CHECK( tx.query_value< int >( "SELECT count(*) FROM tag_displays" ) == 1 );
				}
			}

			AND_WHEN( "the display row is removed" )
			{
				tx.exec( "DELETE FROM tag_displays WHERE tag_id = $1", pqxx::params { tag_id } );

				THEN( "it falls back to the folded text again" )
				{
					CHECK(
						tx.query_value< std::string >( RESOLVE_DISPLAY, pqxx::params { tag_id } )
						== "character:samus aran" );
				}
			}

			AND_WHEN( "a second display row is inserted for the same tag" )
			{
				THEN( "the insert is rejected" )
				{
					CHECK_THROWS_AS(
						tx.exec(
							"INSERT INTO tag_displays (tag_id, display_text) VALUES ($1, 'second')",
							pqxx::params { tag_id } ),
						pqxx::unique_violation );
				}
			}
		}
	}

	GIVEN( "a tag id that does not exist" )
	{
		WHEN( "a display is attached to it" )
		{
			THEN( "the insert is rejected" )
			{
				CHECK_THROWS_AS(
					tx.exec( "INSERT INTO tag_displays (tag_id, display_text) VALUES (99999, 'nope')" ),
					pqxx::foreign_key_violation );
			}
		}
	}
}

} // namespace idhan::test
