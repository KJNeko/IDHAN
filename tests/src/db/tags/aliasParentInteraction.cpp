#include "db/fixtures/MappingFixture.hpp"

TEST_F( MappingFixture, AliasThenParentPropagation )
{
	const auto tag_a { createTag( "interact:alias_source" ) };
	const auto tag_b { createTag( "interact:alias_target" ) };
	const auto tag_parent { createTag( "interact:parent" ) };

	createAlias( tag_a, tag_b );
	createParent( tag_parent, tag_b );

	const auto record { createRecord( "alias_then_parent" ) };
	createMapping( tag_a, record );

	// tag_parent is virtual parent through alias (internal_count=1)
	ASSERT_TRUE( parentInternalExists( record, tag_parent, tag_b, 1 ) );
}

TEST_F( MappingFixture, ParentThenAliasPropagation )
{
	const auto tag_a { createTag( "interact:alias_source2" ) };
	const auto tag_b { createTag( "interact:alias_target2" ) };
	const auto tag_parent { createTag( "interact:parent2" ) };

	createParent( tag_parent, tag_b );
	createAlias( tag_a, tag_b );

	const auto record { createRecord( "parent_then_alias" ) };
	createMapping( tag_a, record );

	ASSERT_TRUE( parentInternalExists( record, tag_parent, tag_b, 1 ) );
}

TEST_F( MappingFixture, MapAliasedDirectly )
{
	const auto tag_a { createTag( "interact:alias_source3" ) };
	const auto tag_b { createTag( "interact:alias_target3" ) };

	createAlias( tag_a, tag_b );

	const auto record { createRecord( "map_aliased_directly" ) };
	createMapping( tag_a, record );

	// Both A and its alias target B should appear (via the final view)
	ASSERT_TRUE( activeMappingExists( record, tag_a ) );
	ASSERT_TRUE( finalViewMappingExists( record, tag_b ) );
}

TEST_F( MappingFixture, ParentChainThroughAlias )
{
	const auto tag_a { createTag( "interact:chain_alias" ) };
	const auto tag_b { createTag( "interact:chain_target" ) };
	const auto tag_parent { createTag( "interact:chain_parent" ) };
	const auto tag_grandparent { createTag( "interact:chain_gparent" ) };

	createAlias( tag_a, tag_b );
	createParent( tag_parent, tag_b );
	createParent( tag_grandparent, tag_parent );

	const auto record { createRecord( "chain_through_alias" ) };
	createMapping( tag_a, record );

	// tag_parent is virtual parent through alias (count=1), tag_grandparent is chained (count=1)
	ASSERT_TRUE( parentInternalExists( record, tag_parent, tag_b, 1 ) );
	ASSERT_TRUE( parentInternalExists( record, tag_grandparent, tag_parent, 1 ) );
}

TEST_F( MappingFixture, ParentOfAliasSource )
{
	const auto tag_a { createTag( "interact:parent_of_alias_src" ) };
	const auto tag_b { createTag( "interact:alias_from_parent" ) };
	const auto tag_parent { createTag( "interact:parent_of_a" ) };

	createParent( tag_parent, tag_a );
	createAlias( tag_a, tag_b );

	const auto record { createRecord( "parent_of_source" ) };
	createMapping( tag_a, record );

	// tag_parent is direct parent of tag_a, internal_count=0
	ASSERT_TRUE( parentInternalExists( record, tag_parent, tag_a, 0 ) );

	// tag_b should also be in the final view (alias target)
	ASSERT_TRUE( finalViewMappingExists( record, tag_b ) );
}

TEST_F( MappingFixture, MultipleAliasesToSameTarget )
{
	const auto tag_a1 { createTag( "interact:multi_a1" ) };
	const auto tag_a2 { createTag( "interact:multi_a2" ) };
	const auto tag_target { createTag( "interact:multi_target" ) };
	const auto tag_parent { createTag( "interact:multi_parent" ) };

	createAlias( tag_a1, tag_target );
	createAlias( tag_a2, tag_target );
	createParent( tag_parent, tag_target );

	const auto record { createRecord( "multi_alias_same_target" ) };
	createMapping( tag_a1, record );
	createMapping( tag_a2, record );

	// tag_target should appear in the final view (alias resolution)
	ASSERT_TRUE( finalViewMappingExists( record, tag_target ) );

	// tag_parent should appear with internal_count from both children
	// tag_a1 -> tag_target -> parent and tag_a2 -> tag_target -> parent
	// Both produce the same (record_id, tag_parent, tag_target) row,
	// internal_count should be 2 reflecting both origins.
	pqxx::work tx { *conn };
	const auto parent_row { tx.exec_params(
		"SELECT internal_count FROM active_tag_mappings_parents WHERE record_id = $1 AND tag_id = $2 AND tag_domain_id = $3 AND origin_id = $4",
		pqxx::params { record, tag_parent, default_domain_id, tag_target } ) };
	ASSERT_FALSE( parent_row.empty() );
	ASSERT_EQ( parent_row[ 0 ][ 0 ].as< std::uint32_t >(), 2 );
}
