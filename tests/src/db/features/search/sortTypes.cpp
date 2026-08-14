#include <algorithm>

#include "api/search/parseSortType.hpp"
#include "core/search/SearchBuilder.hpp"
#include "db/fixtures/SearchFixture.hpp"

using idhan::SearchBuilder;
using idhan::SortOrder;
using idhan::SortType;

//! Position of \p id within \p ids, or npos if absent. Used to assert relative ordering without
//! depending on other rows that may share the same domain-agnostic query.
std::size_t indexOf( const std::vector< RecordID >& ids, const RecordID id )
{
	const auto it { std::ranges::find( ids, id ) };
	return it == ids.end() ? std::string::npos : static_cast< std::size_t >( it - ids.begin() );
}


TEST_F( SearchFixture, RecordTimeOrdersByCreationTimeAndDoesNotError )
{
	const auto r_old { createSearchableRecord( "record_time_old", 100, "image/jpeg", -300 ) };
	const auto r_mid { createSearchableRecord( "record_time_mid", 100, "image/jpeg", -60 ) };
	const auto r_new { createSearchableRecord( "record_time_new", 100, "image/jpeg", 0 ) };

	const auto ids { sortedIds( SortType::RECORD_TIME ) };

	EXPECT_LT( indexOf( ids, r_old ), indexOf( ids, r_mid ) );
	EXPECT_LT( indexOf( ids, r_mid ), indexOf( ids, r_new ) );
}

TEST_F( SearchFixture, ModifiedTimeOrdersAndExcludesUnmodifiedRecords )
{
	const auto r_old { createSearchableRecord( "modified_old", 100, "image/jpeg", -300 ) };
	const auto r_new { createSearchableRecord( "modified_new", 100, "image/jpeg", 0 ) };

	const auto r_unmodified { createSearchableRecord( "modified_none" ) };
	{
		pqxx::work tx { *conn };
		tx.exec_params( "UPDATE file_info SET modified_time = NULL WHERE record_id = $1", pqxx::params { r_unmodified } );
		tx.commit();
	}

	const auto ids { sortedIds( SortType::MODIFIED_TIME ) };

	EXPECT_EQ( ids.size(), 2u );
	EXPECT_LT( indexOf( ids, r_old ), indexOf( ids, r_new ) );
	EXPECT_EQ( indexOf( ids, r_unmodified ), std::string::npos );
}

TEST_F( SearchFixture, MimeOrdersByMimeId )
{
	const auto jpeg_id { getMimeId( "image/jpeg" ) };
	const auto mp4_id { getMimeId( "video/mp4" ) };

	const auto r_jpeg { createSearchableRecord( "mime_jpeg", 100, "image/jpeg" ) };
	const auto r_mp4 { createSearchableRecord( "mime_mp4", 100, "video/mp4" ) };

	const auto ids { sortedIds( SortType::MIME ) };

	// only asserts relative order matches mime_id order, not any particular absolute mime_id
	if ( jpeg_id < mp4_id )
		EXPECT_LT( indexOf( ids, r_jpeg ), indexOf( ids, r_mp4 ) );
	else
		EXPECT_LT( indexOf( ids, r_mp4 ), indexOf( ids, r_jpeg ) );
}

TEST_F( SearchFixture, HashOrdersBySha256 )
{
	const auto r_a { createSearchableRecord( "hash_aaa" ) };
	const auto r_b { createSearchableRecord( "hash_bbb" ) };

	const auto ids { sortedIds( SortType::HASH ) };

	// both records must appear; exact order depends on the digest, so just confirm no crash/drop
	EXPECT_NE( indexOf( ids, r_a ), std::string::npos );
	EXPECT_NE( indexOf( ids, r_b ), std::string::npos );
}

TEST_F( SearchFixture, RandomReturnsFullSetWithoutErroring )
{
	createSearchableRecord( "random_1" );
	createSearchableRecord( "random_2" );
	createSearchableRecord( "random_3" );

	const auto ids { sortedIds( SortType::RANDOM ) };

	// order is inherently unassertable; only the full-count-without-error property is checked
	EXPECT_EQ( ids.size(), 3u );
}

TEST_F( SearchFixture, RandomWorksWithLimitOffset )
{
	createSearchableRecord( "random_lo_1" );
	createSearchableRecord( "random_lo_2" );

	SearchBuilder builder {};
	builder.setSortType( SortType::RANDOM );
	builder.setLimit( 1 );

	const auto ids { runQuery( builder.construct( true, false, false ) ) };

	EXPECT_EQ( ids.size(), 1u );
}

TEST_F( SearchFixture, DurationOrdersAndExcludesNonVideoRecords )
{
	const auto r_short { createSearchableRecord( "duration_short", 100, "video/mp4" ) };
	insertVideoMetadata( r_short, /* duration */ 5.0, 30.0, 640, 480, 1000, false );

	const auto r_long { createSearchableRecord( "duration_long", 100, "video/mp4" ) };
	insertVideoMetadata( r_long, /* duration */ 60.0, 30.0, 640, 480, 1000, false );

	// no video_metadata row at all: NULL duration means "excluded", not "sorts last"
	createSearchableRecord( "duration_none", 100, "image/jpeg" );

	const auto ids_asc { sortedIds( SortType::DURATION, SortOrder::ASC ) };
	EXPECT_EQ( ids_asc.size(), 2u );
	EXPECT_LT( indexOf( ids_asc, r_short ), indexOf( ids_asc, r_long ) );

	const auto ids_desc { sortedIds( SortType::DURATION, SortOrder::DESC ) };
	EXPECT_EQ( ids_desc.size(), 2u );
	EXPECT_LT( indexOf( ids_desc, r_long ), indexOf( ids_desc, r_short ) );
}

TEST_F( SearchFixture, FramerateOrdersAndExcludesNonVideoRecords )
{
	const auto r_low { createSearchableRecord( "framerate_low", 100, "video/mp4" ) };
	insertVideoMetadata( r_low, 10.0, 24.0, 640, 480, 1000, false );

	const auto r_high { createSearchableRecord( "framerate_high", 100, "video/mp4" ) };
	insertVideoMetadata( r_high, 10.0, 60.0, 640, 480, 1000, false );

	createSearchableRecord( "framerate_none", 100, "image/jpeg" );

	const auto ids { sortedIds( SortType::FRAMERATE ) };
	EXPECT_EQ( ids.size(), 2u );
	EXPECT_LT( indexOf( ids, r_low ), indexOf( ids, r_high ) );
}

TEST_F( SearchFixture, HasAudioOrdersAndExcludesNonVideoRecords )
{
	const auto r_silent { createSearchableRecord( "audio_false", 100, "video/mp4" ) };
	insertVideoMetadata( r_silent, 10.0, 30.0, 640, 480, 1000, false );

	const auto r_audio { createSearchableRecord( "audio_true", 100, "video/mp4" ) };
	insertVideoMetadata( r_audio, 10.0, 30.0, 640, 480, 1000, true );

	createSearchableRecord( "audio_none", 100, "image/jpeg" );

	const auto ids { sortedIds( SortType::HAS_AUDIO, SortOrder::ASC ) };
	EXPECT_EQ( ids.size(), 2u );
	EXPECT_LT( indexOf( ids, r_silent ), indexOf( ids, r_audio ) );
}

TEST_F( SearchFixture, WidthOrdersAcrossAllThreeMetadataTablesAndExcludesRecordsWithNone )
{
	const auto r_image { createSearchableRecord( "width_image", 100, "image/jpeg" ) };
	insertImageMetadata( r_image, 100, 100 );

	const auto r_video { createSearchableRecord( "width_video", 100, "video/mp4" ) };
	insertVideoMetadata( r_video, 10.0, 30.0, 500, 500, 1000, false );

	const auto r_project { createSearchableRecord( "width_project", 100, "image/jpeg" ) };
	insertImageProjectMetadata( r_project, 900, 900 );

	// no resolution data anywhere: excluded, not sorted last
	createSearchableRecord( "width_none" );

	const auto ids { sortedIds( SortType::WIDTH ) };
	EXPECT_EQ( ids.size(), 3u );
	EXPECT_LT( indexOf( ids, r_image ), indexOf( ids, r_video ) );
	EXPECT_LT( indexOf( ids, r_video ), indexOf( ids, r_project ) );
}

TEST_F( SearchFixture, HeightOrdersAndExcludesRecordsWithNone )
{
	const auto r_short { createSearchableRecord( "height_short", 100, "image/jpeg" ) };
	insertImageMetadata( r_short, 100, 50 );

	const auto r_tall { createSearchableRecord( "height_tall", 100, "image/jpeg" ) };
	insertImageMetadata( r_tall, 100, 900 );

	createSearchableRecord( "height_none" );

	const auto ids { sortedIds( SortType::HEIGHT ) };
	EXPECT_EQ( ids.size(), 2u );
	EXPECT_LT( indexOf( ids, r_short ), indexOf( ids, r_tall ) );
}

TEST_F( SearchFixture, RatioExcludesZeroHeightAndMissingResolution )
{
	const auto r_wide { createSearchableRecord( "ratio_wide", 100, "image/jpeg" ) };
	insertImageMetadata( r_wide, 16, 9 );

	const auto r_tall { createSearchableRecord( "ratio_tall", 100, "image/jpeg" ) };
	insertImageMetadata( r_tall, 9, 16 );

	const auto r_zero_height { createSearchableRecord( "ratio_zero_height", 100, "image/jpeg" ) };
	insertImageMetadata( r_zero_height, 16, 0 );

	createSearchableRecord( "ratio_none" );

	const auto ids { sortedIds( SortType::RATIO, SortOrder::ASC ) };
	EXPECT_EQ( ids.size(), 2u );
	EXPECT_LT( indexOf( ids, r_tall ), indexOf( ids, r_wide ) );
	EXPECT_EQ( indexOf( ids, r_zero_height ), std::string::npos );
}

TEST_F( SearchFixture, NumPixelsHandlesLargeResolutionWithoutOverflowAndExcludesMissing )
{
	const auto r_small { createSearchableRecord( "pixels_small", 100, "image/jpeg" ) };
	insertImageMetadata( r_small, 10, 10 ); // 100

	const auto r_huge { createSearchableRecord( "pixels_huge", 100, "image/jpeg" ) };
	insertImageMetadata( r_huge, 50000, 50000 );

	createSearchableRecord( "pixels_none" );

	const auto ids { sortedIds( SortType::NUM_PIXELS, SortOrder::ASC ) };
	EXPECT_EQ( ids.size(), 2u );
	EXPECT_LT( indexOf( ids, r_small ), indexOf( ids, r_huge ) );
}

TEST_F( SearchFixture, NumTagsCountsZeroDirectAndParentImpliedTagsAndNeverExcludes )
{
	const auto tag_direct { createTag( "num_tags:direct" ) };
	const auto tag_parent { createTag( "num_tags:parent" ) };
	const auto tag_child { createTag( "num_tags:child" ) };
	createParent( tag_parent, tag_child );

	const auto r_zero { createSearchableRecord( "num_tags_zero" ) };

	const auto r_one { createSearchableRecord( "num_tags_one" ) };
	createMapping( tag_direct, r_one );

	const auto r_parent_implied { createSearchableRecord( "num_tags_parent_implied" ) };
	createMapping( tag_child, r_parent_implied );

	const auto ids { sortedIds( SortType::NUM_TAGS, SortOrder::ASC ) };
	EXPECT_EQ( ids.size(), 3u );
	EXPECT_LT( indexOf( ids, r_zero ), indexOf( ids, r_one ) );
	// r_parent_implied has 2 active tags (child + implied parent), r_one has 1
	EXPECT_LT( indexOf( ids, r_one ), indexOf( ids, r_parent_implied ) );
}

TEST_F( SearchFixture, ParseSortTypeCoversEveryKey )
{
	using idhan::api::parseSortType;

	EXPECT_EQ( parseSortType( "filesize" ), SortType::FILESIZE );
	EXPECT_EQ( parseSortType( "size" ), SortType::FILESIZE );
	EXPECT_EQ( parseSortType( "import_time" ), SortType::IMPORT_TIME );
	EXPECT_EQ( parseSortType( "record_time" ), SortType::RECORD_TIME );
	EXPECT_EQ( parseSortType( "creation_time" ), SortType::RECORD_TIME );
	EXPECT_EQ( parseSortType( "modified_time" ), SortType::MODIFIED_TIME );
	EXPECT_EQ( parseSortType( "mime" ), SortType::MIME );
	EXPECT_EQ( parseSortType( "filetype" ), SortType::MIME );
	EXPECT_EQ( parseSortType( "hash" ), SortType::HASH );
	EXPECT_EQ( parseSortType( "random" ), SortType::RANDOM );
	EXPECT_EQ( parseSortType( "duration" ), SortType::DURATION );
	EXPECT_EQ( parseSortType( "framerate" ), SortType::FRAMERATE );
	EXPECT_EQ( parseSortType( "has_audio" ), SortType::HAS_AUDIO );
	EXPECT_EQ( parseSortType( "width" ), SortType::WIDTH );
	EXPECT_EQ( parseSortType( "height" ), SortType::HEIGHT );
	EXPECT_EQ( parseSortType( "ratio" ), SortType::RATIO );
	EXPECT_EQ( parseSortType( "num_pixels" ), SortType::NUM_PIXELS );
	EXPECT_EQ( parseSortType( "num_tags" ), SortType::NUM_TAGS );
	// unknown values fall back to import time
	EXPECT_EQ( parseSortType( "not_a_real_sort_key" ), SortType::IMPORT_TIME );
}

TEST_F( SearchFixture, FastPathAndGeneralPathAgreeOnOrdering )
{
	const auto tag { createTag( "sort_parity:tag" ) };

	const auto r_a { createSearchableRecord( "parity_a", 10 ) };
	const auto r_b { createSearchableRecord( "parity_b", 20 ) };
	const auto r_c { createSearchableRecord( "parity_c", 30 ) };
	createMapping( tag, r_a );
	createMapping( tag, r_b );
	createMapping( tag, r_c );

	// no filters at all: hits the fast path
	const auto fast_path_ids { sortedIds( SortType::FILESIZE, SortOrder::ASC ) };

	// a positive tag filter that matches everything: hits the general path
	SearchBuilder general_builder {};
	general_builder.setSortType( SortType::FILESIZE );
	general_builder.setSortOrder( SortOrder::ASC );
	general_builder.addPositiveTags( { tag } );
	const auto general_path_ids { runQuery( general_builder.construct( true, false, false ) ) };

	ASSERT_EQ( general_path_ids.size(), 3u );
	EXPECT_EQ( indexOf( general_path_ids, r_a ), 0u );
	EXPECT_EQ( indexOf( general_path_ids, r_b ), 1u );
	EXPECT_EQ( indexOf( general_path_ids, r_c ), 2u );

	// both paths must agree on the relative order of these three records
	EXPECT_LT( indexOf( fast_path_ids, r_a ), indexOf( fast_path_ids, r_b ) );
	EXPECT_LT( indexOf( fast_path_ids, r_b ), indexOf( fast_path_ids, r_c ) );
}
