#include "db/fixtures/MappingFixture.hpp"

TEST_F( MappingFixture, DirectParentPropagation )
{
	const auto tag_child { createTag( "parent:child" ) };
	const auto tag_parent { createTag( "parent:parent" ) };

	createParent( tag_parent, tag_child );

	const auto record { createRecord( "direct_parent_record" ) };
	createMapping( tag_child, record );

	ASSERT_TRUE( parentInternalExists( record, tag_parent, tag_child, 0 ) );
}

TEST_F( MappingFixture, GrandparentPropagation )
{
	const auto tag_child { createTag( "parent:grandchild" ) };
	const auto tag_parent { createTag( "parent:intermediate" ) };
	const auto tag_grandparent { createTag( "parent:grandparent" ) };

	createParent( tag_parent, tag_child );
	createParent( tag_grandparent, tag_parent );

	const auto record { createRecord( "grandparent_record" ) };
	createMapping( tag_child, record );

	// Direct parent has internal_count=0, grandparent (chained) has internal_count=1
	ASSERT_TRUE( parentInternalExists( record, tag_parent, tag_child, 0 ) );
	ASSERT_TRUE( parentInternalExists( record, tag_grandparent, tag_parent, 1 ) );
}

TEST_F( MappingFixture, ParentChainThreeDeep )
{
	const auto tag_c1 { createTag( "parent:c1" ) };
	const auto tag_c2 { createTag( "parent:c2" ) };
	const auto tag_c3 { createTag( "parent:c3" ) };

	createParent( tag_c2, tag_c1 );
	createParent( tag_c3, tag_c2 );

	const auto record { createRecord( "chain3_record" ) };
	createMapping( tag_c1, record );

	// tag_c2 is direct parent, tag_c3 is chained grandparent
	ASSERT_TRUE( parentInternalExists( record, tag_c2, tag_c1, 0 ) );
	ASSERT_TRUE( parentInternalExists( record, tag_c3, tag_c2, 1 ) );
}

TEST_F( MappingFixture, MultipleChildrenShareParent )
{
	const auto tag_child_a { createTag( "parent:child_a" ) };
	const auto tag_child_b { createTag( "parent:child_b" ) };
	const auto tag_parent { createTag( "parent:shared_parent" ) };

	createParent( tag_parent, tag_child_a );
	createParent( tag_parent, tag_child_b );

	const auto record { createRecord( "multi_child_record" ) };
	createMapping( tag_child_a, record );
	createMapping( tag_child_b, record );

	// Both are direct parents (internal_count=0)
	ASSERT_TRUE( parentInternalExists( record, tag_parent, tag_child_a, 0 ) );
	ASSERT_TRUE( parentInternalExists( record, tag_parent, tag_child_b, 0 ) );
}

TEST_F( MappingFixture, ParentPropagatesOnExistingMapping )
{
	const auto tag_child { createTag( "parent:late_child" ) };
	const auto tag_parent { createTag( "parent:late_parent" ) };

	// Map first, then create parent relationship
	const auto record { createRecord( "late_parent_record" ) };
	createMapping( tag_child, record );

	ASSERT_TRUE( activeMappingExists( record, tag_child ) );

	std::size_t count_before { countMappingsForRecord( record ) };

	createParent( tag_parent, tag_child );

	// Parent is direct, internal_count=0
	ASSERT_TRUE( parentInternalExists( record, tag_parent, tag_child, 0 ) );
}

TEST_F( MappingFixture, RemoveMappingCleansUpParent )
{
	const auto tag_child { createTag( "parent:cleanup_child" ) };
	const auto tag_parent { createTag( "parent:cleanup_parent" ) };

	createParent( tag_parent, tag_child );

	const auto record { createRecord( "cleanup_record" ) };
	createMapping( tag_child, record );

	ASSERT_TRUE( parentInternalExists( record, tag_parent, tag_child, 0 ) );

	deleteMapping( tag_child, record );

	// Parent should be gone too
	ASSERT_FALSE( activeMappingExists( record, tag_child ) );

	pqxx::work tx { *conn };
	const auto parent_remaining { tx.exec(
		"SELECT EXISTS(SELECT 1 FROM active_tag_mappings_parents WHERE record_id = $1 AND tag_domain_id = $2)",
		pqxx::params { record, default_domain_id } ) };
	ASSERT_FALSE( parent_remaining[ 0 ][ 0 ].as< bool >() );
}

TEST_F( MappingFixture, DiamondParentPropagation )
{
	const auto tag_child { createTag( "parent:diamond_child" ) };
	const auto tag_parent_a { createTag( "parent:diamond_a" ) };
	const auto tag_parent_b { createTag( "parent:diamond_b" ) };

	createParent( tag_parent_a, tag_child );
	createParent( tag_parent_b, tag_child );

	const auto record { createRecord( "diamond_record" ) };
	createMapping( tag_child, record );

	// Both are direct parents (internal_count=0)
	ASSERT_TRUE( parentInternalExists( record, tag_parent_a, tag_child, 0 ) );
	ASSERT_TRUE( parentInternalExists( record, tag_parent_b, tag_child, 0 ) );
}

TEST_F( MappingFixture, RemovedChildDoesNotPropagateParent )
{
	const auto tag_child { createTag( "parent:removed_child" ) };
	const auto tag_other { createTag( "parent:other" ) };
	const auto tag_parent { createTag( "parent:removed_parent" ) };

	createParent( tag_parent, tag_child );

	const auto record { createRecord( "removed_record" ) };
	createMapping( tag_child, record );

	ASSERT_TRUE( parentInternalExists( record, tag_parent, tag_child, 0 ) );

	// Now map an unrelated tag to the same record
	createMapping( tag_other, record );

	// The parent mapping should still be there and still have count 0
	ASSERT_TRUE( parentInternalExists( record, tag_parent, tag_child, 0 ) );

	deleteMapping( tag_child, record );

	ASSERT_FALSE( activeMappingExists( record, tag_child ) );

	pqxx::work tx { *conn };
	const auto parent_remaining { tx.exec_params(
		"SELECT EXISTS(SELECT 1 FROM active_tag_mappings_parents WHERE record_id = $1 AND tag_domain_id = $2)",
		pqxx::params { record, default_domain_id } ) };
	ASSERT_FALSE( parent_remaining[ 0 ][ 0 ].as< bool >() );
}

TEST_F( MappingFixture, ParentCycleRejected )
{
	const auto tag_a { createTag( "parent:cycle_a" ) };
	const auto tag_b { createTag( "parent:cycle_b" ) };
	const auto tag_c { createTag( "parent:cycle_c" ) };

	createParent( tag_a, tag_b );
	createParent( tag_b, tag_c );

	// Now try to close the cycle: tag_c → tag_a
	ASSERT_ANY_THROW( createParent( tag_c, tag_a ) );

	// Chain should still be intact
	ASSERT_TRUE( parentExists( tag_a, tag_b ) );
	ASSERT_TRUE( parentExists( tag_b, tag_c ) );
	ASSERT_FALSE( parentExists( tag_c, tag_a ) );
}

TEST_F( MappingFixture, RemoveParentCleansUp )
{
	const auto tag_child { createTag( "parent:rm_child" ) };
	const auto tag_parent { createTag( "parent:rm_parent" ) };
	const auto tag_grandparent { createTag( "parent:rm_gparent" ) };

	createParent( tag_parent, tag_child );
	createParent( tag_grandparent, tag_parent );

	const auto record { createRecord( "rm_parent_record" ) };
	createMapping( tag_child, record );

	// Direct parent has internal_count=0, chained grandparent has internal_count=1
	ASSERT_TRUE( parentInternalExists( record, tag_parent, tag_child, 0 ) );
	ASSERT_TRUE( parentInternalExists( record, tag_grandparent, tag_parent, 1 ) );

	// Remove the direct parent
	removeParent( tag_parent, tag_child );

	// Direct parent mapping should be gone
	ASSERT_FALSE( parentMappingExists( record, tag_parent, tag_child ) );

	// Grandparent mapping should also be gone (its chain is broken)
	ASSERT_FALSE( parentMappingExists( record, tag_grandparent, tag_parent ) );

	// Child mapping should remain
	ASSERT_TRUE( activeMappingExists( record, tag_child ) );
}

TEST_F( MappingFixture, ParentChainFiveDeep )
{
	std::vector< TagID > tags;
	for ( int i = 0; i < 5; ++i ) tags.push_back( createTag( std::format( "parent:deep_{}", i ) ) );

	// Create chain: 0→1→2→3→4 so tag_4 is leaf child, tag_0 is root parent
	for ( int i = 4; i > 0; --i ) createParent( tags[ i - 1 ], tags[ i ] );

	const auto record { createRecord( "five_deep_record" ) };
	createMapping( tags[ 4 ], record );

	// Direct parent (tags[3]→tags[4]) has internal_count=0; chained parents have internal_count=1
	ASSERT_TRUE( parentInternalExists( record, tags[ 3 ], tags[ 4 ], 0 ) );
	ASSERT_TRUE( parentInternalExists( record, tags[ 2 ], tags[ 3 ], 1 ) );
	ASSERT_TRUE( parentInternalExists( record, tags[ 1 ], tags[ 2 ], 1 ) );
	ASSERT_TRUE( parentInternalExists( record, tags[ 0 ], tags[ 1 ], 1 ) );
}

TEST_F( MappingFixture, SharedGrandparentInternalCount )
{
	const auto child_a { createTag( "parent:shared_child_a" ) };
	const auto child_b { createTag( "parent:shared_child_b" ) };
	const auto parent { createTag( "parent:shared_parent" ) };
	const auto grandparent { createTag( "parent:shared_gparent" ) };

	// Both children share the same parent
	createParent( parent, child_a );
	createParent( parent, child_b );
	// Parent has its own parent
	createParent( grandparent, parent );

	const auto record { createRecord( "shared_gparent_record" ) };
	createMapping( child_a, record );
	createMapping( child_b, record );

	// Direct parent rows have internal_count=0; grandparent (chained) has internal_count=2
	ASSERT_TRUE( parentInternalExists( record, parent, child_a, 0 ) );
	ASSERT_TRUE( parentInternalExists( record, parent, child_b, 0 ) );
	ASSERT_TRUE( parentInternalExists( record, grandparent, parent, 2 ) );

	// Two direct parent rows + one grandparent row = 3
	ASSERT_EQ( countParentMappings( record ), 3 );
}
