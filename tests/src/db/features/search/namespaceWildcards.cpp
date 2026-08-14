#include <algorithm>

#include "core/search/SearchBuilder.hpp"
#include "db/fixtures/SearchFixture.hpp"

using idhan::NamespaceID;
using idhan::SearchBuilder;

bool contains( const std::vector< RecordID >& ids, const RecordID id )
{
	return std::ranges::find( ids, id ) != ids.end();
}


//! Exercises the `namespace:*` wildcard, which resolves to the filter_namespace_N / filter_namespaces
//! CTE chain in SearchBuilder::construct(). Those CTEs join positive_filter, so they only make sense
//! emitted after it. A mis-ordered WITH clause fails outright, which every test here would catch.
class NamespaceWildcardFixture : public SearchFixture
{
  protected:

	//! createTag() creates the namespace as a side effect; this reads back the id it was given.
	NamespaceID getNamespaceID( const std::string_view namespace_text )
	{
		pqxx::work tx { *conn };

		const auto result { tx.exec_params(
			"SELECT namespace_id FROM tag_namespaces WHERE namespace_text = $1", pqxx::params { namespace_text } ) };

		if ( result.empty() ) throw std::runtime_error( "Namespace does not exist" );

		return result[ 0 ][ 0 ].as< NamespaceID >();
	}

	//! Creates a searchable record and applies every given tag to it.
	RecordID taggedRecord( const std::string_view data, const std::vector< std::string >& tags )
	{
		const auto record_id { createSearchableRecord( data ) };
		for ( const auto& tag : tags ) createMapping( createTag( tag ), record_id );
		return record_id;
	}
};

TEST_F( NamespaceWildcardFixture, MatchesOnlyRecordsCarryingThatNamespace )
{
	const auto with_ns { taggedRecord( "ns_match_with", { "character:alice" } ) };
	const auto without_ns { taggedRecord( "ns_match_without", { "series:midnight" } ) };

	SearchBuilder builder {};
	builder.addNamespaces( { getNamespaceID( "character" ) } );

	const auto ids { runQuery( builder.construct( true, false, false ) ) };

	EXPECT_TRUE( contains( ids, with_ns ) );
	EXPECT_FALSE( contains( ids, without_ns ) );
}

TEST_F( NamespaceWildcardFixture, IntersectsWithPositiveTags )
{
	const auto series_tag { createTag( "series:midnight" ) };

	const auto both { createSearchableRecord( "ns_intersect_both" ) };
	createMapping( series_tag, both );
	createMapping( createTag( "character:alice" ), both );

	// has the positive tag but nothing in the wildcarded namespace
	const auto tag_only { createSearchableRecord( "ns_intersect_tag_only" ) };
	createMapping( series_tag, tag_only );

	// has the namespace but not the positive tag
	const auto namespace_only { taggedRecord( "ns_intersect_ns_only", { "character:bob" } ) };

	SearchBuilder builder {};
	builder.addPositiveTags( { series_tag } );
	builder.addNamespaces( { getNamespaceID( "character" ) } );

	const auto ids { runQuery( builder.construct( true, false, false ) ) };

	EXPECT_TRUE( contains( ids, both ) );
	EXPECT_FALSE( contains( ids, tag_only ) );
	EXPECT_FALSE( contains( ids, namespace_only ) );
}

TEST_F( NamespaceWildcardFixture, MultipleNamespacesAreAllRequired )
{
	const auto both { taggedRecord( "ns_multi_both", { "character:alice", "series:midnight" } ) };
	const auto character_only { taggedRecord( "ns_multi_character", { "character:bob" } ) };
	const auto series_only { taggedRecord( "ns_multi_series", { "series:daybreak" } ) };

	SearchBuilder builder {};
	builder.addNamespaces( { getNamespaceID( "character" ), getNamespaceID( "series" ) } );

	const auto ids { runQuery( builder.construct( true, false, false ) ) };

	EXPECT_TRUE( contains( ids, both ) );
	EXPECT_FALSE( contains( ids, character_only ) );
	EXPECT_FALSE( contains( ids, series_only ) );
}

TEST_F( NamespaceWildcardFixture, NegativeTagsStillSubtractFromNamespaceMatches )
{
	const auto excluded_tag { createTag( "series:midnight" ) };

	const auto keep { taggedRecord( "ns_negative_keep", { "character:alice" } ) };

	const auto drop { createSearchableRecord( "ns_negative_drop" ) };
	createMapping( createTag( "character:bob" ), drop );
	createMapping( excluded_tag, drop );

	SearchBuilder builder {};
	builder.addNamespaces( { getNamespaceID( "character" ) } );
	builder.addNegativeTags( { excluded_tag } );

	const auto ids { runQuery( builder.construct( true, false, false ) ) };

	// INTERSECT binds tighter than EXCEPT, so the namespace narrowing happens before this subtraction
	EXPECT_TRUE( contains( ids, keep ) );
	EXPECT_FALSE( contains( ids, drop ) );
}

TEST_F( NamespaceWildcardFixture, DuplicateNamespacesDoNotBreakTheQuery )
{
	const auto with_ns { taggedRecord( "ns_duplicate", { "character:alice" } ) };

	const auto character_ns { getNamespaceID( "character" ) };

	SearchBuilder builder {};
	// a duplicate id would emit `filter_namespace_N` twice, which postgres rejects outright
	builder.addNamespaces( { character_ns, character_ns } );
	builder.addNamespaces( { character_ns } );

	const auto ids { runQuery( builder.construct( true, false, false ) ) };

	EXPECT_TRUE( contains( ids, with_ns ) );
}

TEST_F( NamespaceWildcardFixture, NamespaceWildcardDisablesTheNoFilterFastPath )
{
	const auto with_ns { taggedRecord( "ns_fastpath_with", { "character:alice" } ) };
	const auto without_ns { taggedRecord( "ns_fastpath_without", { "series:midnight" } ) };

	SearchBuilder builder {};
	builder.addNamespaces( { getNamespaceID( "character" ) } );

	const auto sql { builder.construct( true, false, false ) };
	ASSERT_TRUE( sql.starts_with( "WITH " ) );

	const auto ids { runQuery( sql ) };

	EXPECT_TRUE( contains( ids, with_ns ) );
	EXPECT_FALSE( contains( ids, without_ns ) );
}
