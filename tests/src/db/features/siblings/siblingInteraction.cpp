#include "db/fixtures/MappingFixture.hpp"

class SiblingFixture : public MappingFixture
{
  protected:

	void createSibling( TagID older_id, TagID younger_id )
	{
		pqxx::work tx { *conn };
		tx.exec_params(
			"INSERT INTO tag_siblings (older_id, younger_id, tag_domain_id) VALUES ($1, $2, $3)",
			pqxx::params { older_id, younger_id, default_domain_id } );
		tx.commit();
	}

	bool isTagSilenced( RecordID record_id, TagID tag_id )
	{
		pqxx::work tx { *conn };
		const auto result { tx.exec_params(
			"SELECT silenced FROM active_tag_mappings WHERE record_id = $1 AND tag_id = $2 AND tag_domain_id = $3",
			pqxx::params { record_id, tag_id, default_domain_id } ) };
		if ( result.empty() ) return false;
		return result[ 0 ][ 0 ].as< bool >();
	}

	bool olderSiblingPairExists( TagID older_id, TagID younger_id )
	{
		pqxx::work tx { *conn };
		const auto result { tx.exec_params(
			"SELECT EXISTS(SELECT 1 FROM aliased_siblings WHERE older_id = $1 AND younger_id = $2 AND tag_domain_id = $3)",
			pqxx::params { older_id, younger_id, default_domain_id } ) };
		return result[ 0 ][ 0 ].as< bool >();
	}
};

TEST_F( SiblingFixture, DirectSiblingSilencing )
{
	const auto older { createTag( "sibling:older" ) };
	const auto younger { createTag( "sibling:younger" ) };

	createSibling( older, younger );

	const auto record { createRecord( "direct_sibling" ) };
	createMapping( older, record );
	createMapping( younger, record );

	EXPECT_TRUE( isTagSilenced( record, younger ) );
	EXPECT_FALSE( isTagSilenced( record, older ) );
}

TEST_F( SiblingFixture, NoSilencingWithoutOlder )
{
	const auto older { createTag( "sibling:no_old_a" ) };
	const auto younger { createTag( "sibling:no_old_b" ) };

	createSibling( older, younger );

	const auto record { createRecord( "no_older" ) };
	createMapping( younger, record );

	EXPECT_FALSE( isTagSilenced( record, younger ) );
}

TEST_F( SiblingFixture, TransitiveChainInView )
{
	const auto a { createTag( "sibling:trans_a" ) };
	const auto b { createTag( "sibling:trans_b" ) };
	const auto c { createTag( "sibling:trans_c" ) };

	createSibling( a, b );
	createSibling( b, c );

	EXPECT_TRUE( olderSiblingPairExists( a, b ) );
	EXPECT_TRUE( olderSiblingPairExists( b, c ) );
	EXPECT_TRUE( olderSiblingPairExists( a, c ) );
}

TEST_F( SiblingFixture, SiblingAfterMappingSilences )
{
	const auto older { createTag( "sibling:after_older" ) };
	const auto younger { createTag( "sibling:after_younger" ) };

	createSibling( older, younger );

	const auto record { createRecord( "sibling_after" ) };
	createMapping( younger, record );
	EXPECT_FALSE( isTagSilenced( record, younger ) );

	createMapping( older, record );
	EXPECT_TRUE( isTagSilenced( record, younger ) );
}

TEST_F( SiblingFixture, SiblingChainSilencing )
{
	const auto a { createTag( "sibling:chain_a" ) };
	const auto b { createTag( "sibling:chain_b" ) };
	const auto c { createTag( "sibling:chain_c" ) };

	createSibling( a, b );
	createSibling( b, c );

	const auto record { createRecord( "chain_silence" ) };
	createMapping( a, record );
	createMapping( c, record );

	EXPECT_TRUE( isTagSilenced( record, c ) );
	EXPECT_FALSE( isTagSilenced( record, a ) );
}

TEST_F( SiblingFixture, StorageCountIncludesSilenced )
{
	const auto older { createTag( "sibling:storage_old" ) };
	const auto younger { createTag( "sibling:storage_young" ) };

	createSibling( older, younger );

	const auto record { createRecord( "storage_count" ) };
	createMapping( older, record );
	createMapping( younger, record );

	EXPECT_EQ( getTagStorageCount( younger ), 1 );
	EXPECT_EQ( getTagStorageCount( older ), 1 );
}

TEST_F( SiblingFixture, DisplayCountExcludesSilenced )
{
	const auto older { createTag( "sibling:display_old" ) };
	const auto younger { createTag( "sibling:display_young" ) };

	createSibling( older, younger );

	const auto record { createRecord( "display_count" ) };
	createMapping( older, record );
	createMapping( younger, record );

	EXPECT_EQ( getTagDisplayCount( younger ), 0 );
	EXPECT_EQ( getTagDisplayCount( older ), 1 );
}

TEST_F( SiblingFixture, DeleteMappingUnsilences )
{
	const auto older { createTag( "sibling:unsil_old" ) };
	const auto younger { createTag( "sibling:unsil_young" ) };

	createSibling( older, younger );

	const auto record { createRecord( "unsilence" ) };
	createMapping( older, record );
	createMapping( younger, record );

	EXPECT_TRUE( isTagSilenced( record, younger ) );

	deleteMapping( older, record );

	EXPECT_FALSE( isTagSilenced( record, younger ) );
}

TEST_F( SiblingFixture, SiblingWithAliasResolution )
{
	const auto older_raw { createTag( "sibling:alias_old_raw" ) };
	const auto older_ideal { createTag( "sibling:alias_old_ideal" ) };
	const auto younger_raw { createTag( "sibling:alias_young_raw" ) };
	const auto younger_ideal { createTag( "sibling:alias_young_ideal" ) };

	createAlias( older_raw, older_ideal );
	createAlias( younger_raw, younger_ideal );
	createSibling( older_raw, younger_raw );

	const auto record { createRecord( "alias_sibling" ) };
	createMapping( older_raw, record );
	createMapping( younger_raw, record );

	EXPECT_TRUE( olderSiblingPairExists( older_ideal, younger_ideal ) );
}

TEST_F( SiblingFixture, OlderSiblingInParentSilencesYounger )
{
	const auto explicit_tag { createTag( "sibling:silence_explicit" ) };
	const auto questionable { createTag( "sibling:silence_questionable" ) };
	const auto safe { createTag( "sibling:silence_safe" ) };
	const auto pussy { createTag( "sibling:silence_pussy" ) };

	createSibling( explicit_tag, questionable );
	createSibling( questionable, safe );
	createParent( questionable, pussy );

	const auto record { createRecord( "parent_silence" ) };
	createMapping( safe, record );
	createMapping( pussy, record );

	EXPECT_TRUE( parentMappingExists( record, explicit_tag, pussy ) );
	EXPECT_TRUE( isTagSilenced( record, safe ) );
	EXPECT_FALSE( isTagSilenced( record, pussy ) );
}

TEST_F( SiblingFixture, ParentReplacedByOlderSibling )
{
	const auto explicit_tag { createTag( "sibling:explicit" ) };
	const auto questionable { createTag( "sibling:questionable" ) };
	const auto pussy { createTag( "sibling:pussy" ) };

	createSibling( explicit_tag, questionable );
	createParent( questionable, pussy );

	const auto record { createRecord( "parent_replaced" ) };
	createMapping( pussy, record );

	EXPECT_TRUE( parentMappingExists( record, explicit_tag, pussy ) );
}

TEST_F( SiblingFixture, ParentReplacedByOlderSiblingMaintainsSilencing )
{
	const auto explicit_tag { createTag( "sibling:maintain_explicit" ) };
	const auto questionable { createTag( "sibling:maintain_questionable" ) };
	const auto safe { createTag( "sibling:maintain_safe" ) };
	const auto pussy { createTag( "sibling:maintain_pussy" ) };

	createSibling( explicit_tag, questionable );
	createSibling( questionable, safe );
	createParent( questionable, pussy );

	const auto record { createRecord( "maintain_silence" ) };
	createMapping( safe, record );
	createMapping( pussy, record );

	EXPECT_TRUE( isTagSilenced( record, safe ) );
	EXPECT_TRUE( parentMappingExists( record, explicit_tag, pussy ) );

	EXPECT_EQ( getTagStorageCount( safe ), 1 );
	EXPECT_EQ( getTagDisplayCount( safe ), 0 );
	EXPECT_EQ( getTagStorageCount( explicit_tag ), 0 );
	EXPECT_EQ( getTagDisplayCount( explicit_tag ), 0 );
}

TEST_F( SiblingFixture, FinalViewExcludesSilenced )
{
	const auto older { createTag( "sibling:final_old" ) };
	const auto younger { createTag( "sibling:final_young" ) };

	createSibling( older, younger );

	const auto record { createRecord( "final_view" ) };
	createMapping( older, record );
	createMapping( younger, record );

	EXPECT_TRUE( finalViewMappingExists( record, older ) );
	EXPECT_FALSE( finalViewMappingExists( record, younger ) );
}
