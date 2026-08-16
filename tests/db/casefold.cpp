#include "MigratedSchema.hpp"
#include "TagHelpers.hpp"

namespace idhan::test
{

namespace
{

// Spelled out as UTF-8 byte escapes rather than as characters. The precomposed and decomposed forms are
// indistinguishable on screen, so anything that normalizes this file on save would turn one into the other
// and leave every test below passing without testing anything.
// TheTwoSpellingsDifferBeforeTheyReachThePostgres pins the byte layout so that cannot happen quietly.

//! "Amelie" carrying a precomposed U+00E9: U+0041 U+006D U+00E9 U+006C U+0069 U+0065
constexpr std::string_view AMELIE_PRECOMPOSED {
	"Am"
	"\xc3\xa9"
	"lie"
};

//! "Amelie" carrying "e" plus a combining acute: U+0041 U+006D U+0065 U+0301 U+006C U+0069 U+0065
constexpr std::string_view AMELIE_DECOMPOSED {
	"Ame"
	"\xcc\x81"
	"lie"
};

//! What both spellings have to collapse to once casefolded and composed back to NFC.
constexpr std::string_view AMELIE_FOLDED {
	"am"
	"\xc3\xa9"
	"lie"
};

// Amelie is unified by either normalize pass on its own, so it cannot tell them apart. The dotted capital I
// is the one letter where postgres' casefold does not preserve canonical equivalence: folding the
// precomposed U+0130 gives "i", folding the decomposed spelling gives "i" plus a combining dot that will not
// compose away afterwards. Only normalizing BEFORE the fold unifies the two.

//! Turkish dotted capital I, precomposed: U+0130
constexpr std::string_view DOTTED_I_PRECOMPOSED { "\xc4\xb0" };

//! The same letter as "I" plus a combining dot above: U+0049 U+0307
constexpr std::string_view DOTTED_I_DECOMPOSED {
	"I"
	"\xcc\x87"
};

//! Both spellings have to land on a plain "i".
constexpr std::string_view DOTTED_I_FOLDED { "i" };

} // namespace

TEST_F( MigratedSchema, TheTwoSpellingsDifferBeforeTheyReachThePostgres )
{
	ASSERT_NE( AMELIE_PRECOMPOSED, AMELIE_DECOMPOSED );
	ASSERT_EQ( AMELIE_PRECOMPOSED.size(), 7 );
	ASSERT_EQ( AMELIE_DECOMPOSED.size(), 8 );

	ASSERT_NE( DOTTED_I_PRECOMPOSED, DOTTED_I_DECOMPOSED );
	ASSERT_EQ( DOTTED_I_PRECOMPOSED.size(), 2 );
	ASSERT_EQ( DOTTED_I_DECOMPOSED.size(), 3 );
}

TEST_F( MigratedSchema, SubtagFoldsCanonicallyEquivalentSpellingsTogether )
{
	pqxx::work tx { connection() };

	const auto precomposed_id { insertTag( tx, "character", AMELIE_PRECOMPOSED ) };
	const auto decomposed_id { insertTag( tx, "character", AMELIE_DECOMPOSED ) };

	EXPECT_EQ( precomposed_id, decomposed_id );
	EXPECT_EQ( tx.query_value< int >( "SELECT count(*) FROM tags" ), 1 );
	EXPECT_EQ( tx.query_value< std::string >( "SELECT subtag_text FROM tags" ), std::string( AMELIE_FOLDED ) );
}

TEST_F( MigratedSchema, SubtagFoldsTheSameWayWhenTheDecomposedSpellingArrivesFirst )
{
	pqxx::work tx { connection() };

	const auto decomposed_id { insertTag( tx, "character", AMELIE_DECOMPOSED ) };
	const auto precomposed_id { insertTag( tx, "character", AMELIE_PRECOMPOSED ) };

	EXPECT_EQ( decomposed_id, precomposed_id );
	EXPECT_EQ( tx.query_value< int >( "SELECT count(*) FROM tags" ), 1 );
	EXPECT_EQ( tx.query_value< std::string >( "SELECT subtag_text FROM tags" ), std::string( AMELIE_FOLDED ) );
}

TEST_F( MigratedSchema, SubtagFoldsTheDottedCapitalITogetherWithItsDecomposedSpelling )
{
	pqxx::work tx { connection() };

	const auto precomposed_id { insertTag( tx, "character", DOTTED_I_PRECOMPOSED ) };
	const auto decomposed_id { insertTag( tx, "character", DOTTED_I_DECOMPOSED ) };

	EXPECT_EQ( precomposed_id, decomposed_id );
	EXPECT_EQ( tx.query_value< int >( "SELECT count(*) FROM tags" ), 1 );
	EXPECT_EQ( tx.query_value< std::string >( "SELECT subtag_text FROM tags" ), std::string( DOTTED_I_FOLDED ) );
}

TEST_F( MigratedSchema, TagTextStaysFoldedForATagBuiltFromMixedCaseParts )
{
	pqxx::work tx { connection() };

	insertTag( tx, "Character", AMELIE_PRECOMPOSED );

	EXPECT_EQ(
		tx.query_value< std::string >( "SELECT tag_text FROM tags" ), "character:" + std::string( AMELIE_FOLDED ) );
}

TEST_F( MigratedSchema, NamespaceFoldsCanonicallyEquivalentSpellingsTogether )
{
	pqxx::work tx { connection() };

	const auto precomposed_id { insertNamespace( tx, AMELIE_PRECOMPOSED ) };
	const auto decomposed_id { insertNamespace( tx, AMELIE_DECOMPOSED ) };

	EXPECT_EQ( precomposed_id, decomposed_id );
	EXPECT_EQ( tx.query_value< int >( "SELECT count(*) FROM tag_namespaces" ), 1 );
	EXPECT_EQ(
		tx.query_value< std::string >( "SELECT namespace_text FROM tag_namespaces" ), std::string( AMELIE_FOLDED ) );
}

} // namespace idhan::test
