#include "db/fixtures/MappingFixture.hpp"

TEST_F( MappingFixture, MappingIncrementsStorageCount )
{
	const auto tag { createTag( "count:increment" ) };

	ASSERT_EQ( getTagStorageCount( tag ), 0 );
	ASSERT_EQ( getTagDisplayCount( tag ), 0 );

	const auto record { createRecord( "count_record" ) };
	createMapping( tag, record );

	ASSERT_EQ( getTagStorageCount( tag ), 1 );
	ASSERT_EQ( getTagDisplayCount( tag ), 1 );
}

TEST_F( MappingFixture, MultipleMappingsIncrementCount )
{
	const auto tag { createTag( "count:multiple" ) };

	const auto record_a { createRecord( "count_record_a" ) };
	const auto record_b { createRecord( "count_record_b" ) };
	const auto record_c { createRecord( "count_record_c" ) };

	createMapping( tag, record_a );
	ASSERT_EQ( getTagStorageCount( tag ), 1 );

	createMapping( tag, record_b );
	ASSERT_EQ( getTagStorageCount( tag ), 2 );

	createMapping( tag, record_c );
	ASSERT_EQ( getTagStorageCount( tag ), 3 );
}

TEST_F( MappingFixture, DeleteDecrementsCount )
{
	const auto tag { createTag( "count:decrement" ) };

	const auto record { createRecord( "count_delete" ) };
	createMapping( tag, record );
	ASSERT_EQ( getTagStorageCount( tag ), 1 );

	deleteMapping( tag, record );
	ASSERT_EQ( getTagStorageCount( tag ), 0 );
	ASSERT_EQ( getTagDisplayCount( tag ), 0 );
}

TEST_F( MappingFixture, DifferentTagsHaveIndependentCounts )
{
	const auto tag_a { createTag( "count:indep_a" ) };
	const auto tag_b { createTag( "count:indep_b" ) };

	const auto record { createRecord( "count_indep" ) };
	createMapping( tag_a, record );
	createMapping( tag_b, record );

	ASSERT_EQ( getTagStorageCount( tag_a ), 1 );
	ASSERT_EQ( getTagStorageCount( tag_b ), 1 );
	ASSERT_EQ( getTagStorageCount( tag_a ), 1 );
}

TEST_F( MappingFixture, DoubleMappingSameRecordDoesNotDoubleCount )
{
	const auto tag { createTag( "count:no_duplicate" ) };

	const auto record { createRecord( "count_no_double" ) };
	createMapping( tag, record );

	pqxx::work tx { *conn };
	tx.exec_params(
		"INSERT INTO tag_mappings (tag_id, record_id, tag_domain_id) VALUES ($1, $2, $3) ON CONFLICT DO NOTHING",
		pqxx::params { tag, record, default_domain_id } );
	tx.commit();

	ASSERT_EQ( getTagStorageCount( tag ), 1 );
}

TEST_F( MappingFixture, DisplayCountWithAlias )
{
	const auto tag_a { createTag( "count:alias_display_a" ) };
	const auto tag_b { createTag( "count:alias_display_b" ) };

	createAlias( tag_a, tag_b );

	const auto record { createRecord( "count_alias_display" ) };
	createMapping( tag_a, record );

	ASSERT_EQ( getTagStorageCount( tag_a ), 1 );
	// storage_count only tracks the stored tag, not alias-resolved tag
	ASSERT_EQ( getTagStorageCount( tag_b ), 0 );

	// display_count tracks the resolved (displayed) tag, not the stored tag
	ASSERT_EQ( getTagDisplayCount( tag_a ), 0 );
	ASSERT_EQ( getTagDisplayCount( tag_b ), 1 );
}

TEST_F( MappingFixture, CountsEmptyForUnmappedTag )
{
	const auto tag { createTag( "count:unmapped" ) };

	// Tag with no mappings should have no tag_counts row
	ASSERT_FALSE( tagCountsExist( tag ) );

	// Helpers should return 0
	ASSERT_EQ( getTagStorageCount( tag ), 0 );
	ASSERT_EQ( getTagDisplayCount( tag ), 0 );
}

TEST_F( MappingFixture, CountWithAliasedParentPropagation )
{
	const auto tag_a { createTag( "count:alias_parent_a" ) };
	const auto tag_b { createTag( "count:alias_parent_b" ) };
	const auto tag_parent { createTag( "count:alias_parent_p" ) };

	createAlias( tag_a, tag_b );
	createParent( tag_parent, tag_b );

	const auto record { createRecord( "count_alias_parent" ) };
	createMapping( tag_a, record );

	// tag_a is mapped directly, so storage_count = 1
	ASSERT_EQ( getTagStorageCount( tag_a ), 1 );

	// storage_count only tracks stored tags, not alias-resolved tags
	ASSERT_EQ( getTagStorageCount( tag_b ), 0 );

	// tag_parent (through alias target tag_b) has internal_count=1
	ASSERT_TRUE( parentInternalExists( record, tag_parent, tag_b, 1 ) );

	// Verify the final view includes the parent
	ASSERT_TRUE( finalViewMappingExists( record, tag_parent ) );

	// Total final view count = resolved alias (tag_b replaces tag_a) + parent (tag_parent)
	ASSERT_EQ( countFinalViewMappings( record ), 2 );
}
