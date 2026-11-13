#include <algorithm>

#include <catch2/catch_test_macros.hpp>

#include "MimeIDs.hpp"

namespace idhan::test
{

//! Every id in a block sits between the block's base and the next block's base.
static bool inBlock( const MimeID id, const MimeID block )
{
	return id >= block && id < block + 1000;
}

SCENARIO( "Well known mime ids", "[mime][ids]" )
{
	GIVEN( "the closed mime id set" )
	{
		WHEN( "every declared constant is looked up" )
		{
			THEN( "each one has a name" )
			{
				for ( const auto id : mime_ids::ALL_MIME_IDS )
				{
					INFO( "mime id " << id );
					const auto itter { mime_ids::mime_names.find( id ) };
					REQUIRE( itter != mime_ids::mime_names.end() );
					REQUIRE_FALSE( itter->second.empty() );
				}
			}

			THEN( "the map holds nothing beyond them" )
			{
				REQUIRE( mime_ids::mime_names.size() == mime_ids::ALL_MIME_IDS.size() );
			}

			THEN( "each id has a best extension" )
			{
				REQUIRE( mime_ids::mime_extensions.size() == mime_ids::ALL_MIME_IDS.size() );
				for ( const auto id : mime_ids::ALL_MIME_IDS )
				{
					INFO( "mime id " << id );
					REQUIRE( mime_ids::mime_extensions.contains( id ) );
					if ( id == mime_ids::UNKNOWN ) continue;

					REQUIRE_FALSE( mime_ids::mime_extensions.at( id ).empty() );
				}

				REQUIRE( mime_ids::mime_extensions.at( mime_ids::UNKNOWN ).empty() );
				REQUIRE( mime_ids::mime_extensions.at( mime_ids::IMAGE_JPEG ) == "jpg" );
				REQUIRE( mime_ids::mime_extensions.at( mime_ids::ANIMATION_APNG ) == "png" );
				REQUIRE( mime_ids::mime_extensions.at( mime_ids::APPLICATION_ZIP ) == "zip" );
				REQUIRE( mime_ids::mime_extensions.at( mime_ids::COMICBOOK_ZIP ) == "cbz" );
				REQUIRE( mime_ids::mime_extensions.at( mime_ids::PIXIV_UGOIRA ) == "zip" );
			}
		}

		WHEN( "the invalid id is looked up" )
		{
			THEN( "it is not a mime" )
			{
				REQUIRE( mime_ids::INVALID == 0 );
				REQUIRE( mime_ids::mime_names.find( mime_ids::INVALID ) == mime_ids::mime_names.end() );
				REQUIRE_FALSE( mime_ids::mime_extensions.contains( mime_ids::INVALID ) );
				REQUIRE(
					std::ranges::find( mime_ids::ALL_MIME_IDS, mime_ids::INVALID ) == mime_ids::ALL_MIME_IDS.end() );
			}
		}

		WHEN( "ids are checked against their category block" )
		{
			THEN( "each sits in the block its name implies" )
			{
				REQUIRE( inBlock( mime_ids::IMAGE_JPEG, 1000 ) );
				REQUIRE( inBlock( mime_ids::IMAGE_PNG, 1000 ) );
				REQUIRE( inBlock( mime_ids::IMAGE_WEBP, 1000 ) );
				REQUIRE( inBlock( mime_ids::IMAGE_AVIF, 1000 ) );
				REQUIRE( inBlock( mime_ids::IMAGE_TIFF, 1000 ) );
				REQUIRE( inBlock( mime_ids::VIDEO_MP4, 2000 ) );
				REQUIRE( inBlock( mime_ids::VIDEO_MPEG, 2000 ) );
				REQUIRE( inBlock( mime_ids::VIDEO_WEBM, 2000 ) );
				REQUIRE( inBlock( mime_ids::VIDEO_QUICKTIME, 2000 ) );
				REQUIRE( inBlock( mime_ids::ANIMATION_GIF, 3000 ) );
				REQUIRE( inBlock( mime_ids::ANIMATION_APNG, 3000 ) );
				REQUIRE( inBlock( mime_ids::APPLICATION_ZIP, 5000 ) );
				REQUIRE( inBlock( mime_ids::COMICBOOK_ZIP, 5000 ) );
				REQUIRE( inBlock( mime_ids::PIXIV_UGOIRA, 5000 ) );
				REQUIRE( inBlock( mime_ids::APPLICATION_PSD, 6000 ) );
				REQUIRE( inBlock( mime_ids::APPLICATION_CLIP, 6000 ) );
			}
		}

		WHEN( "a specialized type is compared to its base type" )
		{
			THEN( "a ugoira reports the same string as a plain zip" )
			{
				REQUIRE( mime_ids::PIXIV_UGOIRA != mime_ids::APPLICATION_ZIP );
				REQUIRE(
					mime_ids::mime_names.at( mime_ids::PIXIV_UGOIRA )
					== mime_ids::mime_names.at( mime_ids::APPLICATION_ZIP ) );
			}

		}
	}
}

} // namespace idhan::test
