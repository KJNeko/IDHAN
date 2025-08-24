
#include "MappingFixture.hpp"
#include "ServerTagFixture.hpp"

TEST_F( MappingFixture, TagParentCreation )
{
	const auto tag_ahri { createTag( "ahri (league of legends)" ) };
	const auto tag_kogmaw { createTag( "kogmaw (league of legends)" ) };

	const auto tag_league { createTag( "series:league of legends" ) };
	const auto tag_riot_games { createTag( "copyright:riot games" ) };

	// test adding to existing
	const auto record_1 { createRecord( "record_1" ) };

	createMapping( tag_ahri, record_1 );
	createMapping( tag_kogmaw, record_1 );

	createParent( tag_league, tag_ahri );
	createParent( tag_league, tag_kogmaw );

	createParent( tag_riot_games, tag_league );

	ASSERT_TRUE( parentExists( tag_league, tag_ahri ) );
	ASSERT_TRUE( parentExists( tag_league, tag_kogmaw ) );

	// test adding to new mappings
	const auto record_2 { createRecord( "record_2" ) };

	createMapping( tag_ahri, record_2 );
	createMapping( tag_kogmaw, record_2 );

	ASSERT_TRUE( parentInternalExists( record_1, tag_riot_games, tag_league, 2 ) );
	ASSERT_TRUE( parentInternalExists( record_2, tag_riot_games, tag_league, 2 ) );
}
