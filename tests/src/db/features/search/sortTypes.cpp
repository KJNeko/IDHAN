#include <algorithm>

#include "core/search/SearchBuilder.hpp"
#include "db/fixtures/SearchFixture.hpp"

using idhan::SearchBuilder;
using idhan::SortOrder;
using idhan::SortType;

namespace
{

//! Position of \p id within \p ids, or npos if absent — used to assert relative ordering without
//! depending on other rows that may share the same domain-agnostic query.
std::size_t indexOf( const std::vector< RecordID >& ids, const RecordID id )
{
	const auto it { std::ranges::find( ids, id ) };
	return it == ids.end() ? std::string::npos : static_cast< std::size_t >( it - ids.begin() );
}

} // namespace

TEST_F( SearchFixture, RecordTimeOrdersByCreationTimeAndDoesNotError )
{
	// this is also the regression test for the pre-existing "records.creation_time doesn't exist"
	// bug: prior to the 194-records.sql migration this sort type errored unconditionally.
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

	// createSearchableRecord always stamps modified_time; NULL it out here to model a record
	// that's never actually been modified, which sorting by modified_time should exclude
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

	// no video_metadata row at all — NULL duration means "excluded", not "sorts last"
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
	general_builder.setPositiveTags( { tag } );
	const auto general_path_ids { runQuery( general_builder.construct( true, false, false ) ) };

	ASSERT_EQ( general_path_ids.size(), 3u );
	EXPECT_EQ( indexOf( general_path_ids, r_a ), 0u );
	EXPECT_EQ( indexOf( general_path_ids, r_b ), 1u );
	EXPECT_EQ( indexOf( general_path_ids, r_c ), 2u );

	// both paths must agree on the relative order of these three records
	EXPECT_LT( indexOf( fast_path_ids, r_a ), indexOf( fast_path_ids, r_b ) );
	EXPECT_LT( indexOf( fast_path_ids, r_b ), indexOf( fast_path_ids, r_c ) );
}
