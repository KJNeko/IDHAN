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
		}

		WHEN( "the invalid id is looked up" )
		{
			THEN( "it is not a mime" )
			{
				REQUIRE( mime_ids::INVALID == 0 );
				REQUIRE( mime_ids::mime_names.find( mime_ids::INVALID ) == mime_ids::mime_names.end() );
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

		WHEN( "a refined type is compared to the generic one" )
		{
			THEN( "a ugoira reports the same string as a plain zip" )
			{
				REQUIRE( mime_ids::PIXIV_UGOIRA != mime_ids::APPLICATION_ZIP );
				REQUIRE(
					mime_ids::mime_names.at( mime_ids::PIXIV_UGOIRA )
					== mime_ids::mime_names.at( mime_ids::APPLICATION_ZIP ) );
			}

			THEN( "a bare application/zip resolves to the plain zip, never the ugoira" )
			{
				REQUIRE( mime_ids::canonicalIDForName( "application/zip" ) == mime_ids::APPLICATION_ZIP );
			}

			THEN( "a type carrying its own name resolves to itself" )
			{
				REQUIRE(
					mime_ids::canonicalIDForName( "application/vnd.comicbook+zip" ) == mime_ids::COMICBOOK_ZIP );
			}

			THEN( "a name no id carries resolves to nothing" )
			{
				REQUIRE( mime_ids::canonicalIDForName( "application/never-declared" ) == mime_ids::INVALID );
			}
		}

		WHEN( "every declared name is resolved" )
		{
			THEN( "it lands on an id that actually reports that name" )
			{
				for ( const auto id : mime_ids::ALL_MIME_IDS )
				{
					const auto name { mime_ids::mime_names.at( id ) };

					INFO( "mime id " << id << " named " << name );
					REQUIRE( mime_ids::mime_names.at( mime_ids::canonicalIDForName( name ) ) == name );
				}
			}

			THEN( "it lands on the lowest id carrying that name" )
			{
				for ( const auto id : mime_ids::ALL_MIME_IDS )
				{
					const auto name { mime_ids::mime_names.at( id ) };

					INFO( "mime id " << id << " named " << name );
					REQUIRE( mime_ids::canonicalIDForName( name ) <= id );
				}
			}
		}
	}
}

} // namespace idhan::test
