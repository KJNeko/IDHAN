#include "MigratedSchema.hpp"
#include "TagHelpers.hpp"

namespace idhan::test
{

namespace
{

//! How a reader is meant to resolve a tag's display form: the override if there is one, the folded text if not.
constexpr std::string_view RESOLVE_DISPLAY {
	"SELECT COALESCE(d.display_text, t.tag_text) FROM tags t "
	"LEFT JOIN tag_displays d USING (tag_id) WHERE t.tag_id = $1"
};

void setDisplay( pqxx::transaction_base& tx, const int tag_id, const std::string_view display )
{
	tx.exec(
		"INSERT INTO tag_displays (tag_id, display_text) VALUES ($1, $2) "
		"ON CONFLICT (tag_id) DO UPDATE SET display_text = EXCLUDED.display_text",
		pqxx::params { tag_id, display } );
}

} // namespace

TEST_F( MigratedSchema, ATagWithNoDisplayRowResolvesToItsFoldedText )
{
	pqxx::work tx { connection() };

	const auto tag_id { insertTag( tx, "Character", "Samus Aran" ) };

	EXPECT_EQ( tx.query_value< int >( "SELECT count(*) FROM tag_displays" ), 0 );
	EXPECT_EQ( tx.query_value< std::string >( RESOLVE_DISPLAY, pqxx::params { tag_id } ), "character:samus aran" );
}

TEST_F( MigratedSchema, ADisplayRowOverridesTheFoldedText )
{
	pqxx::work tx { connection() };

	const auto tag_id { insertTag( tx, "Character", "Samus Aran" ) };
	setDisplay( tx, tag_id, "Character:Samus Aran" );

	EXPECT_EQ( tx.query_value< std::string >( RESOLVE_DISPLAY, pqxx::params { tag_id } ), "Character:Samus Aran" );
	EXPECT_EQ( tx.query_value< std::string >( "SELECT tag_text FROM tags" ), "character:samus aran" );
}

TEST_F( MigratedSchema, ADisplayCanBeSetLongAfterTheTagExistsAndChangedAgainLater )
{
	pqxx::work tx { connection() };

	const auto tag_id { insertTag( tx, "Character", "Samus Aran" ) };

	setDisplay( tx, tag_id, "Character:Samus Aran" );
	EXPECT_EQ( tx.query_value< std::string >( RESOLVE_DISPLAY, pqxx::params { tag_id } ), "Character:Samus Aran" );

	setDisplay( tx, tag_id, "character:SAMUS ARAN" );
	EXPECT_EQ( tx.query_value< std::string >( RESOLVE_DISPLAY, pqxx::params { tag_id } ), "character:SAMUS ARAN" );

	EXPECT_EQ( tx.query_value< int >( "SELECT count(*) FROM tag_displays" ), 1 );
}

TEST_F( MigratedSchema, RemovingTheDisplayRowFallsBackToTheFoldedTextAgain )
{
	pqxx::work tx { connection() };

	const auto tag_id { insertTag( tx, "Character", "Samus Aran" ) };
	setDisplay( tx, tag_id, "Character:Samus Aran" );

	tx.exec( "DELETE FROM tag_displays WHERE tag_id = $1", pqxx::params { tag_id } );

	EXPECT_EQ( tx.query_value< std::string >( RESOLVE_DISPLAY, pqxx::params { tag_id } ), "character:samus aran" );
}

TEST_F( MigratedSchema, ADisplayCannotBeAttachedToATagThatDoesNotExist )
{
	pqxx::work tx { connection() };

	EXPECT_THROW(
		tx.exec( "INSERT INTO tag_displays (tag_id, display_text) VALUES (99999, 'nope')" ),
		pqxx::foreign_key_violation );
}

TEST_F( MigratedSchema, ATagCarriesAtMostOneDisplayRow )
{
	pqxx::work tx { connection() };

	const auto tag_id { insertTag( tx, "Character", "Samus Aran" ) };
	setDisplay( tx, tag_id, "Character:Samus Aran" );

	EXPECT_THROW(
		tx.exec( "INSERT INTO tag_displays (tag_id, display_text) VALUES ($1, 'second')", pqxx::params { tag_id } ),
		pqxx::unique_violation );
}

} // namespace idhan::test
