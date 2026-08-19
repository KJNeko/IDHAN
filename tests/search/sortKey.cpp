#include <gtest/gtest.h>

#include "core/search/SortKey.hpp"

using namespace idhan;
using namespace idhan::search;

//! Every sort type IDHAN can be asked for, so a new one cannot skip these checks.
static constexpr std::array< SortType, 15 > ALL_SORT_TYPES { SortType::FILESIZE,   SortType::IMPORT_TIME,
	                                                         SortType::RECORD_TIME, SortType::MODIFIED_TIME,
	                                                         SortType::MIME,        SortType::HASH,
	                                                         SortType::RANDOM,      SortType::DURATION,
	                                                         SortType::FRAMERATE,   SortType::HAS_AUDIO,
	                                                         SortType::WIDTH,       SortType::HEIGHT,
	                                                         SortType::RATIO,       SortType::NUM_PIXELS,
	                                                         SortType::NUM_TAGS };

// A sort orders results; it must never decide which records match. An inner join on an optional
// metadata table silently drops every record missing that metadata, which turned a 112k result set
// into 1.6k when sorting by duration.
TEST( SortKeySpec, VideoBackedSortsDoNotFilterOutNonVideos )
{
	for ( const auto type : { SortType::DURATION, SortType::FRAMERATE, SortType::HAS_AUDIO } )
	{
		const auto spec { sortKeySpec( type ) };

		EXPECT_NE( spec.joins.find( "LEFT JOIN video_metadata" ), std::string_view::npos )
			<< "an inner join to video_metadata drops every non-video record";
		EXPECT_FALSE( spec.exclude_null ) << "a missing video key must order, not exclude";
		EXPECT_NE( spec.expression.find( "COALESCE" ), std::string_view::npos )
			<< "a LEFT joined key needs a defined value for records that did not match";
	}
}

// Silent records and records with no video metadata at all both rank below records with audio.
TEST( SortKeySpec, HasAudioRanksSilentRecordsBelowAudioRecords )
{
	const auto spec { sortKeySpec( SortType::HAS_AUDIO ) };

	EXPECT_EQ( spec.expression, "COALESCE(vm.has_audio::INT, 0)" );
	EXPECT_EQ( spec.type, SortKeyType::Integer );
}

// records is the parent table of every record_id a search can produce, so joining it cannot drop
// rows. Any other inner join in a sort spec can.
TEST( SortKeySpec, NoSortIntroducesAFilteringJoin )
{
	for ( const auto type : ALL_SORT_TYPES )
	{
		const auto spec { sortKeySpec( type ) };

		for ( std::size_t pos = spec.joins.find( " JOIN " ); pos != std::string_view::npos;
		      pos = spec.joins.find( " JOIN ", pos + 1 ) )
		{
			const bool is_left { pos >= 5 && spec.joins.substr( pos - 5, 10 ) == " LEFT JOIN" };
			const bool is_records { spec.joins.compare( pos, 13, " JOIN records" ) == 0 };

			EXPECT_TRUE( is_left || is_records )
				<< "sort type " << static_cast< int >( type ) << " joins with a filtering inner join: "
				<< spec.joins;
		}
	}
}

TEST( SortKeySpec, RandomHasNoKey )
{
	const auto spec { sortKeySpec( SortType::RANDOM ) };

	EXPECT_TRUE( spec.expression.empty() );
	EXPECT_EQ( spec.type, SortKeyType::None );
	EXPECT_FALSE( spec.exclude_null );
}
