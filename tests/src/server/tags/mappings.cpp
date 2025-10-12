//
// Created by kj16609 on 8/18/25.
//

#include "MappingFixture.hpp"

TEST_F( MappingFixture, StorageMapping )
{
	const auto tag_1 { createTag( "tag:1" ) };
	const auto tag_2 { createTag( "tag:2" ) };

	const auto record { createRecord( "record" ) };

	createMapping( tag_1, record );
	createMapping( tag_2, record );

	ASSERT_TRUE( mappingExists( tag_1, record ) );
	ASSERT_TRUE( mappingExists( tag_2, record ) );

	SUCCEED();
}