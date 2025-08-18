//
// Created by kj16609 on 8/18/25.
//

#include "ServerTagFixture.hpp"

TEST_F( ServerTagFixture, TagAliasCreation )
{
	const auto tag_1 { createTag( "tag:1" ) };
	const auto tag_2 { createTag( "tag:2" ) };

	createAlias( tag_1, tag_2 );

	ASSERT_TRUE( aliasExists( tag_1, tag_2 ) );

	SUCCEED();
}

TEST_F( ServerTagFixture, RecursiveProtection )
{
	const auto tag_1 { createTag( "tag:1" ) };
	const auto tag_2 { createTag( "tag:2" ) };
	const auto tag_3 { createTag( "tag:3" ) };

	ASSERT_NO_THROW( createAlias( tag_1, tag_2 ) );
	ASSERT_NO_THROW( createAlias( tag_2, tag_3 ) );

	ASSERT_ANY_THROW( createAlias( tag_2, tag_1 ) );
	ASSERT_ANY_THROW( createAlias( tag_3, tag_1 ) );

	SUCCEED();
}
