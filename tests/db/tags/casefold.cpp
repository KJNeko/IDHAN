#include <catch2/catch_test_macros.hpp>

#include "MigratedSchema.hpp"
#include "TagHelpers.hpp"

namespace idhan::test
{

// Spelled out as UTF-8 byte escapes rather than as characters. The precomposed and decomposed forms are
// indistinguishable on screen, so anything that normalizes this file on save would turn one into the other
// and leave every test below passing without testing anything.
// "Unicode spellings" pins the byte layout so that cannot happen quietly.

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

SCENARIO( "Unicode spellings", "[db][tags][casefold]" )
{
	GIVEN( "the two spellings this file is built on" )
	{
		THEN( "they differ before they ever reach postgres" )
		{
			CHECK( AMELIE_PRECOMPOSED != AMELIE_DECOMPOSED );
			CHECK( AMELIE_PRECOMPOSED.size() == 7 );
			CHECK( AMELIE_DECOMPOSED.size() == 8 );

			CHECK( DOTTED_I_PRECOMPOSED != DOTTED_I_DECOMPOSED );
			CHECK( DOTTED_I_PRECOMPOSED.size() == 2 );
			CHECK( DOTTED_I_DECOMPOSED.size() == 3 );
		}
	}
}

SCENARIO_METHOD( MigratedSchema, "Subtag casefolding", "[db][tags][casefold]" )
{
	pqxx::work tx { connection() };

	GIVEN( "canonically equivalent spellings of a subtag" )
	{
		WHEN( "the precomposed spelling is inserted first" )
		{
			const auto precomposed_id { insertTag( tx, "character", AMELIE_PRECOMPOSED ) };
			const auto decomposed_id { insertTag( tx, "character", AMELIE_DECOMPOSED ) };

			THEN( "both spellings fold onto one tag" )
			{
				CHECK( precomposed_id == decomposed_id );
				CHECK( tx.query_value< int >( "SELECT count(*) FROM tags" ) == 1 );
				CHECK( tx.query_value< std::string >( "SELECT subtag_text FROM tags" ) == AMELIE_FOLDED );
			}
		}

		WHEN( "the decomposed spelling is inserted first" )
		{
			const auto decomposed_id { insertTag( tx, "character", AMELIE_DECOMPOSED ) };
			const auto precomposed_id { insertTag( tx, "character", AMELIE_PRECOMPOSED ) };

			THEN( "both spellings fold onto one tag" )
			{
				CHECK( decomposed_id == precomposed_id );
				CHECK( tx.query_value< int >( "SELECT count(*) FROM tags" ) == 1 );
				CHECK( tx.query_value< std::string >( "SELECT subtag_text FROM tags" ) == AMELIE_FOLDED );
			}
		}
	}

	GIVEN( "the dotted capital I, where casefolding alone does not preserve equivalence" )
	{
		WHEN( "both spellings are inserted" )
		{
			const auto precomposed_id { insertTag( tx, "character", DOTTED_I_PRECOMPOSED ) };
			const auto decomposed_id { insertTag( tx, "character", DOTTED_I_DECOMPOSED ) };

			THEN( "they fold onto one tag holding a plain i" )
			{
				CHECK( precomposed_id == decomposed_id );
				CHECK( tx.query_value< int >( "SELECT count(*) FROM tags" ) == 1 );
				CHECK( tx.query_value< std::string >( "SELECT subtag_text FROM tags" ) == DOTTED_I_FOLDED );
			}
		}
	}

	GIVEN( "a tag built from mixed case parts" )
	{
		WHEN( "it is inserted" )
		{
			insertTag( tx, "Character", AMELIE_PRECOMPOSED );

			THEN( "its tag text stays folded" )
			{
				CHECK(
					tx.query_value< std::string >( "SELECT tag_text FROM tags" )
					== "character:" + std::string( AMELIE_FOLDED ) );
			}
		}
	}
}

SCENARIO_METHOD( MigratedSchema, "Namespace casefolding", "[db][tags][casefold]" )
{
	pqxx::work tx { connection() };

	GIVEN( "canonically equivalent spellings of a namespace" )
	{
		WHEN( "both are inserted" )
		{
			const auto precomposed_id { insertNamespace( tx, AMELIE_PRECOMPOSED ) };
			const auto decomposed_id { insertNamespace( tx, AMELIE_DECOMPOSED ) };

			THEN( "they fold onto one namespace" )
			{
				CHECK( precomposed_id == decomposed_id );
				CHECK( tx.query_value< int >( "SELECT count(*) FROM tag_namespaces" ) == 1 );
				CHECK( tx.query_value< std::string >( "SELECT namespace_text FROM tag_namespaces" ) == AMELIE_FOLDED );
			}
		}
	}
}

} // namespace idhan::test
