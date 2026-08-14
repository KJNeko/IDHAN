#include <algorithm>

#include "core/search/SearchBuilder.hpp"
#include "db/fixtures/SearchFixture.hpp"

using idhan::SearchBuilder;
using idhan::TagID;

bool contains( const std::vector< RecordID >& ids, const RecordID id )
{
	return std::ranges::find( ids, id ) != ids.end();
}


//! Exercises subtag wildcards (`cat*girl`), which resolve to a set of tag ids at parse time and
//! become one `filter_wildcard_N` CTE per wildcard in SearchBuilder::construct().
//!
//! The pattern is matched against the whole `tags.tag_text`, namespace included -- there is no
//! separate namespace/subtag matching step. That one rule is what makes `cat*girl` stay
//! unnamespaced while `*cat girl` reaches into every namespace, and it is what MatchSemantics
//! below pins down.
class SubtagWildcardFixture : public SearchFixture
{
  protected:

	//! The corpus every MatchSemantics case is asserted against. Deliberately includes the
	//! near-misses (`cat girls`, `bobcat girl`) that separate anchoring from substring matching.
	void createTagCorpus()
	{
		for ( const auto& text :
		      { "cat girl",
		        "catgirl",
		        "cat girls",
		        "bobcat girl",
		        "character:cat girl",
		        "character:catgirl",
		        "series:cat girl" } )
			createTag( text );
	}

	//! Resolves \p wildcard exactly as setWildcardTags() does -- same translation, same query -- and
	//! reads the matches back as tag text so the expectations stay legible.
	std::vector< std::string > matchedTags( const std::string_view wildcard )
	{
		const auto pattern { SearchBuilder::wildcardToLikePattern( wildcard ) };

		pqxx::work tx { *conn };

		const auto matched { tx.exec( SearchBuilder::wildcard_tag_query, pqxx::params { pattern } ) };

		std::vector< std::string > texts {};
		for ( const auto& row : matched )
		{
			const auto tag_id { row[ 0 ].as< TagID >() };
			const auto text { tx.exec( "SELECT tag_text FROM tags WHERE tag_id = $1", pqxx::params { tag_id } ) };
			texts.emplace_back( text[ 0 ][ 0 ].as< std::string >() );
		}

		std::ranges::sort( texts );
		return texts;
	}

	//! Resolves \p wildcard to the tag ids it matches, for feeding the builder directly.
	std::vector< TagID > resolveWildcard( const std::string_view wildcard )
	{
		const auto pattern { SearchBuilder::wildcardToLikePattern( wildcard ) };

		pqxx::work tx { *conn };
		const auto matched { tx.exec( SearchBuilder::wildcard_tag_query, pqxx::params { pattern } ) };

		std::vector< TagID > ids {};
		for ( const auto& row : matched ) ids.emplace_back( row[ 0 ].as< TagID >() );
		return ids;
	}

	//! Creates a searchable record and applies every given tag to it.
	RecordID taggedRecord( const std::string_view data, const std::vector< std::string >& tags )
	{
		const auto record_id { createSearchableRecord( data ) };
		for ( const auto& tag : tags ) createMapping( createTag( tag ), record_id );
		return record_id;
	}
};

// --- pattern translation -----------------------------------------------------------------------

TEST_F( SubtagWildcardFixture, StarBecomesPercent )
{
	EXPECT_EQ( SearchBuilder::wildcardToLikePattern( "cat*girl" ), "cat%girl" );
	EXPECT_EQ( SearchBuilder::wildcardToLikePattern( "*cat girl" ), "%cat girl" );
	EXPECT_EQ( SearchBuilder::wildcardToLikePattern( "*:cat*girl" ), "%:cat%girl" );
	EXPECT_EQ( SearchBuilder::wildcardToLikePattern( "*" ), "%" );
}

TEST_F( SubtagWildcardFixture, LikeMetacharactersAreEscaped )
{
	EXPECT_EQ( SearchBuilder::wildcardToLikePattern( "100%*" ), "100\\%%" );
	EXPECT_EQ( SearchBuilder::wildcardToLikePattern( "a_b" ), "a\\_b" );
	EXPECT_EQ( SearchBuilder::wildcardToLikePattern( "back\\slash" ), "back\\\\slash" );
}

// --- match semantics ---------------------------------------------------------------------------

TEST_F( SubtagWildcardFixture, CatStarGirlStaysUnnamespaced )
{
	createTagCorpus();

	EXPECT_EQ( matchedTags( "cat*girl" ), ( std::vector< std::string > { "cat girl", "catgirl" } ) );
}

TEST_F( SubtagWildcardFixture, LeadingStarReachesIntoNamespaces )
{
	createTagCorpus();

	// the leading `*` absorbs `character:`, and equally absorbs the `bob` of `bobcat girl`
	EXPECT_EQ(
		matchedTags( "*cat girl" ),
		( std::vector< std::string > { "bobcat girl", "cat girl", "character:cat girl", "series:cat girl" } ) );
}

TEST_F( SubtagWildcardFixture, StarColonDemandsANamespace )
{
	createTagCorpus();

	// the literal `:` in the pattern is what excludes the unnamespaced `cat girl`
	EXPECT_EQ(
		matchedTags( "*:cat girl" ), ( std::vector< std::string > { "character:cat girl", "series:cat girl" } ) );
}

TEST_F( SubtagWildcardFixture, StarColonCombinesWithAnInnerStar )
{
	createTagCorpus();

	EXPECT_EQ(
		matchedTags( "*:cat*girl" ), ( std::vector< std::string > { "character:cat girl", "character:catgirl" } ) );
}

TEST_F( SubtagWildcardFixture, TrailingStarIsAPrefixSearch )
{
	createTagCorpus();

	EXPECT_EQ( matchedTags( "cat girl*" ), ( std::vector< std::string > { "cat girl", "cat girls" } ) );
}

TEST_F( SubtagWildcardFixture, ExactTextWithNoStarMatchesOnlyItself )
{
	createTagCorpus();

	EXPECT_EQ( matchedTags( "cat girl" ), ( std::vector< std::string > { "cat girl" } ) );
}

TEST_F( SubtagWildcardFixture, LiteralPercentIsNotAWildcard )
{
	createTag( "100% cotton" );
	createTag( "100 proof" );

	// were the `%` passed through unescaped this would also match `100 proof`
	EXPECT_EQ( matchedTags( "100% *" ), ( std::vector< std::string > { "100% cotton" } ) );
}

// --- filter behaviour --------------------------------------------------------------------------

TEST_F( SubtagWildcardFixture, MatchesRecordsCarryingAnyMatchedTag )
{
	const auto spaced { taggedRecord( "wc_any_spaced", { "cat girl" } ) };
	const auto joined { taggedRecord( "wc_any_joined", { "catgirl" } ) };
	const auto plural { taggedRecord( "wc_any_plural", { "cat girls" } ) };

	SearchBuilder builder {};
	builder.addPositiveWildcard( resolveWildcard( "cat*girl" ) );

	const auto ids { runQuery( builder.construct( true, false, false ) ) };

	// the two matched tags are OR'd inside the single filter_wildcard_0 CTE
	EXPECT_TRUE( contains( ids, spaced ) );
	EXPECT_TRUE( contains( ids, joined ) );
	EXPECT_FALSE( contains( ids, plural ) );
}

TEST_F( SubtagWildcardFixture, MultipleWildcardsAreAllRequired )
{
	const auto both { taggedRecord( "wc_multi_both", { "catgirl", "series:midnight" } ) };
	const auto cat_only { taggedRecord( "wc_multi_cat", { "cat girl" } ) };
	const auto series_only { taggedRecord( "wc_multi_series", { "series:daybreak" } ) };

	SearchBuilder builder {};
	builder.addPositiveWildcard( resolveWildcard( "cat*girl" ) );
	builder.addPositiveWildcard( resolveWildcard( "series:*night" ) );

	const auto ids { runQuery( builder.construct( true, false, false ) ) };

	// separate predicates, so they INTERSECT rather than collapsing into one OR'd set
	EXPECT_TRUE( contains( ids, both ) );
	EXPECT_FALSE( contains( ids, cat_only ) );
	EXPECT_FALSE( contains( ids, series_only ) );
}

TEST_F( SubtagWildcardFixture, IntersectsWithPositiveTags )
{
	const auto series_tag { createTag( "series:midnight" ) };

	const auto both { createSearchableRecord( "wc_intersect_both" ) };
	createMapping( series_tag, both );
	createMapping( createTag( "catgirl" ), both );

	const auto tag_only { createSearchableRecord( "wc_intersect_tag_only" ) };
	createMapping( series_tag, tag_only );

	const auto wildcard_only { taggedRecord( "wc_intersect_wildcard_only", { "cat girl" } ) };

	SearchBuilder builder {};
	builder.addPositiveTags( { series_tag } );
	builder.addPositiveWildcard( resolveWildcard( "cat*girl" ) );

	const auto ids { runQuery( builder.construct( true, false, false ) ) };

	EXPECT_TRUE( contains( ids, both ) );
	EXPECT_FALSE( contains( ids, tag_only ) );
	EXPECT_FALSE( contains( ids, wildcard_only ) );
}

TEST_F( SubtagWildcardFixture, NegativeWildcardSubtractsEveryMatch )
{
	const auto keep_tag { createTag( "series:midnight" ) };

	const auto keep { createSearchableRecord( "wc_negative_keep" ) };
	createMapping( keep_tag, keep );

	const auto drop { createSearchableRecord( "wc_negative_drop" ) };
	createMapping( keep_tag, drop );
	createMapping( createTag( "catgirl" ), drop );

	SearchBuilder builder {};
	builder.addPositiveTags( { keep_tag } );
	builder.addNegativeWildcard( resolveWildcard( "cat*girl" ) );

	const auto ids { runQuery( builder.construct( true, false, false ) ) };

	EXPECT_TRUE( contains( ids, keep ) );
	EXPECT_FALSE( contains( ids, drop ) );
}

TEST_F( SubtagWildcardFixture, PositiveAndNegativeWildcardsGetDistinctCteNames )
{
	const auto keep { taggedRecord( "wc_names_keep", { "cat girl" } ) };

	const auto drop { createSearchableRecord( "wc_names_drop" ) };
	createMapping( createTag( "catgirl" ), drop );
	createMapping( createTag( "series:midnight" ), drop );

	SearchBuilder builder {};
	builder.addPositiveWildcard( resolveWildcard( "cat*girl" ) );
	builder.addNegativeWildcard( resolveWildcard( "series:*night" ) );

	const auto ids { runQuery( builder.construct( true, false, false ) ) };

	EXPECT_TRUE( contains( ids, keep ) );
	EXPECT_FALSE( contains( ids, drop ) );
}

TEST_F( SubtagWildcardFixture, FollowsAliasesToTheIdealTag )
{
	const auto ideal { createTag( "catgirl" ) };
	const auto aliased { createTag( "ctgrl" ) };
	createAlias( aliased, ideal );

	// tagged only with the alias, which resolves to a tag the wildcard matched
	const auto record { createSearchableRecord( "wc_alias" ) };
	createMapping( aliased, record );

	SearchBuilder builder {};
	// `ctgrl` is not itself a match for the pattern; only the ideal it resolves to is
	builder.addPositiveWildcard( resolveWildcard( "cat*girl" ) );

	const auto ids { runQuery( builder.construct( true, false, false ) ) };

	EXPECT_TRUE( contains( ids, record ) );
}

TEST_F( SubtagWildcardFixture, WildcardDisablesTheNoFilterFastPath )
{
	const auto matching { taggedRecord( "wc_fastpath_match", { "cat girl" } ) };
	const auto other { taggedRecord( "wc_fastpath_other", { "series:midnight" } ) };

	SearchBuilder builder {};
	builder.addPositiveWildcard( resolveWildcard( "cat*girl" ) );

	const auto sql { builder.construct( true, false, false ) };
	ASSERT_TRUE( sql.starts_with( "WITH " ) );

	const auto ids { runQuery( sql ) };

	EXPECT_TRUE( contains( ids, matching ) );
	EXPECT_FALSE( contains( ids, other ) );
}

TEST_F( SubtagWildcardFixture, EmptyMatchSetMatchesNothing )
{
	const auto record { taggedRecord( "wc_empty", { "cat girl" } ) };

	SearchBuilder builder {};
	builder.addPositiveWildcard( {} );

	const auto ids { runQuery( builder.construct( true, false, false ) ) };

	EXPECT_FALSE( contains( ids, record ) );
	EXPECT_TRUE( ids.empty() );
}
