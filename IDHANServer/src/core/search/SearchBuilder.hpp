//
// Created by kj16609 on 11/7/24.
//
#pragma once

#include <expected>
#include <string_view>

#include "IDHANTypes.hpp"
#include "SearchBuilder.hpp"
#include "api/APIAuth.hpp"
#include "db/dbTypes.hpp"
#include "drogon/HttpRequest.h"
#include "drogon/orm/DbClient.h"
#include "drogon/orm/Result.h"
#include "drogon/utils/coroutine.h"
#include "hydrus/ClientConstants_gen.hpp"

namespace idhan
{

enum class SortOrder
{
	ASC,
	DESC
};

enum class SortType
{
	FILESIZE,
	IMPORT_TIME,
	RECORD_TIME,

	DEFAULT = FILESIZE,

	// BEGIN_HYDRUS_CONVERT
	HY_FILESIZE = FILESIZE,
	HY_DURATION = DEFAULT,
	HY_IMPORT_TIME = IMPORT_TIME,
	HY_MIME = DEFAULT,
	HY_RANDOM = DEFAULT,
	HY_WIDTH = DEFAULT,
	HY_HEIGHT = DEFAULT,
	HY_RATIO = DEFAULT,
	HY_NUM_PIXELS = DEFAULT,
	HY_NUM_TAGS = DEFAULT,
	HY_MEDIA_VIEWS = DEFAULT,
	HY_MEDIA_VIEWTIME = DEFAULT,
	HY_APPROX_BITRATE = DEFAULT,
	HY_HAS_AUDIO = DEFAULT,
	HY_FILE_MODIFIED_TIMESTAMP = DEFAULT,
	HY_FRAMERATE = DEFAULT,
	HY_NUM_FRAMES = DEFAULT,
	HY_NUM_COLLECTION_FILES = DEFAULT,
	HY_LAST_VIEWED_TIME = DEFAULT,
	HY_ARCHIVED_TIMESTAMP = DEFAULT,
	HY_HASH = DEFAULT,
	HY_PIXEL_HASH = DEFAULT,
	HY_BLURHASH = DEFAULT,
	// END_HYDRUS_CONVERT
};

enum HydrusSortType
{
	HY_FILESIZE = hydrus::gen_constants::SORT_FILES_BY_FILESIZE,
	HY_DURATION = hydrus::gen_constants::SORT_FILES_BY_DURATION,
	HY_IMPORT_TIME = hydrus::gen_constants::SORT_FILES_BY_IMPORT_TIME,
	HY_MIME = hydrus::gen_constants::SORT_FILES_BY_MIME,
	HY_RANDOM = hydrus::gen_constants::SORT_FILES_BY_RANDOM,
	HY_WIDTH = hydrus::gen_constants::SORT_FILES_BY_WIDTH,
	HY_HEIGHT = hydrus::gen_constants::SORT_FILES_BY_HEIGHT,
	HY_RATIO = hydrus::gen_constants::SORT_FILES_BY_RATIO,
	HY_NUM_PIXELS = hydrus::gen_constants::SORT_FILES_BY_NUM_PIXELS,
	HY_NUM_TAGS = hydrus::gen_constants::SORT_FILES_BY_NUM_TAGS,
	HY_MEDIA_VIEWS = hydrus::gen_constants::SORT_FILES_BY_MEDIA_VIEWS,
	HY_MEDIA_VIEWTIME = hydrus::gen_constants::SORT_FILES_BY_MEDIA_VIEWTIME,
	HY_APPROX_BITRATE = hydrus::gen_constants::SORT_FILES_BY_APPROX_BITRATE,
	HY_HAS_AUDIO = hydrus::gen_constants::SORT_FILES_BY_HAS_AUDIO,
	HY_FILE_MODIFIED_TIMESTAMP = hydrus::gen_constants::SORT_FILES_BY_FILE_MODIFIED_TIMESTAMP,
	HY_FRAMERATE = hydrus::gen_constants::SORT_FILES_BY_FRAMERATE,
	HY_NUM_FRAMES = hydrus::gen_constants::SORT_FILES_BY_NUM_FRAMES,
	HY_NUM_COLLECTION_FILES = hydrus::gen_constants::SORT_FILES_BY_NUM_COLLECTION_FILES,
	HY_LAST_VIEWED_TIME = hydrus::gen_constants::SORT_FILES_BY_LAST_VIEWED_TIME,
	HY_ARCHIVED_TIMESTAMP = hydrus::gen_constants::SORT_FILES_BY_ARCHIVED_TIMESTAMP,
	HY_HASH = hydrus::gen_constants::SORT_FILES_BY_HASH,
	HY_PIXEL_HASH = hydrus::gen_constants::SORT_FILES_BY_PIXEL_HASH,
	HY_BLURHASH = hydrus::gen_constants::SORT_FILES_BY_BLURHASH,
	DEFAULT = HY_IMPORT_TIME
};

enum class HydrusDisplayType
{
	STORED,
	DISPLAY
};

constexpr SortType hyToIDHANSortType( const HydrusSortType hy_sort )
{
	switch ( hy_sort )
	{
		case HydrusSortType::HY_FILESIZE:
			return SortType::HY_FILESIZE;
		case HydrusSortType::HY_DURATION:
			return SortType::HY_DURATION;
		case HydrusSortType::HY_IMPORT_TIME:
			return SortType::HY_IMPORT_TIME;
		case HydrusSortType::HY_MIME:
			return SortType::HY_MIME;
		case HydrusSortType::HY_RANDOM:
			return SortType::HY_RANDOM;
		case HydrusSortType::HY_WIDTH:
			return SortType::HY_WIDTH;
		case HydrusSortType::HY_HEIGHT:
			return SortType::HY_HEIGHT;
		case HydrusSortType::HY_RATIO:
			return SortType::HY_RATIO;
		case HydrusSortType::HY_NUM_PIXELS:
			return SortType::HY_NUM_PIXELS;
		case HydrusSortType::HY_NUM_TAGS:
			return SortType::HY_NUM_TAGS;
		case HydrusSortType::HY_MEDIA_VIEWS:
			return SortType::HY_MEDIA_VIEWS;
		case HydrusSortType::HY_MEDIA_VIEWTIME:
			return SortType::HY_MEDIA_VIEWTIME;
		case HydrusSortType::HY_APPROX_BITRATE:
			return SortType::HY_APPROX_BITRATE;
		case HydrusSortType::HY_HAS_AUDIO:
			return SortType::HY_HAS_AUDIO;
		case HydrusSortType::HY_FILE_MODIFIED_TIMESTAMP:
			return SortType::HY_FILE_MODIFIED_TIMESTAMP;
		case HydrusSortType::HY_FRAMERATE:
			return SortType::HY_FRAMERATE;
		case HydrusSortType::HY_NUM_FRAMES:
			return SortType::HY_NUM_FRAMES;
		case HydrusSortType::HY_NUM_COLLECTION_FILES:
			return SortType::HY_NUM_COLLECTION_FILES;
		case HydrusSortType::HY_LAST_VIEWED_TIME:
			return SortType::HY_LAST_VIEWED_TIME;
		case HydrusSortType::HY_ARCHIVED_TIMESTAMP:
			return SortType::HY_ARCHIVED_TIMESTAMP;
		case HydrusSortType::HY_HASH:
			return SortType::HY_HASH;
		case HydrusSortType::HY_PIXEL_HASH:
			return SortType::HY_PIXEL_HASH;
		case HydrusSortType::HY_BLURHASH:
			return SortType::HY_BLURHASH;
		default:
			return SortType::DEFAULT;
	}
}

class SearchBuilder
{
	std::string file_records_filter {};

	//! Contains a list of all required joins for this query and it's sorting options
	struct
	{
		bool file_info { false };
		bool records { false };

		bool left_video_metadata { false };
		bool video_metadata { false };

		bool left_image_metadata { false };
		bool image_metadata { false };
	} m_required_joins {};

	bool m_search_everything { false };

	using SearchOperation = std::uint8_t;

	enum SearchOperationFlags : SearchOperation
	{
		GreaterThan = 1 << 0, // >
		LessThan = 1 << 1, // <
		Equal = 1 << 2, // =
		Not = 1 << 3, // !
		// Approximate = 1 << 4, // ~
		Approximate = Equal, // ~

		// helpers
		NotLessThan = Not | LessThan, // ~<
		NotGreaterThan = Not | GreaterThan, // ~>

		GreaterThanEqual = GreaterThan | Equal, // >=
		LessThanEqual = LessThan | Equal, // <=
		NotGreaterThanEqual = Not | GreaterThanEqual, // ~>=
		NotLessThanEqual = Not | LessThanEqual, // ~<=

		NotEqual = Not | Equal, // ~=
	};

	enum class DurationSearchType
	{
		DontCare = 0,
		HasDuration,
		NoDuration
	} m_duration_search { DurationSearchType::DontCare };

	enum class AudioSearchType
	{
		DontCare = 0,
		HasAudio,
		NoAudio
	} m_audio_search { AudioSearchType::DontCare };

	enum class ExitSearchType
	{
		DontCare = 0,
		HasExif,
		NoExif
	} m_exif_search { ExitSearchType::DontCare };

	enum class TagCountSearchType
	{
		DontCare = 0,
		HasTags,
		NoTags,
		HasCount
	} m_has_tags_search { TagCountSearchType::DontCare };

	struct RangeSearchInfo
	{
		//! If true then this count and operation are put into effect
		bool m_active { false };
		std::size_t count { 0 };
		SearchOperation operation { 0 };
	};

	static void parseRangeSearch( RangeSearchInfo& target, std::string_view tag );

	RangeSearchInfo m_tag_count_search {};

	RangeSearchInfo m_width_search {};
	RangeSearchInfo m_height_search {};

	RangeSearchInfo m_limit_search {};

	SortType m_sort_type;
	SortOrder m_order;
	std::vector< TagID > m_positive_tags;
	std::vector< TagID > m_negative_tags {};

	HydrusDisplayType m_display_mode;
	bool m_bind_domains { false };

	static std::unordered_map< TagID, std::string > createFilters(
		const std::vector< TagID >& tag_ids,
		bool filter_domains );
	std::string buildPositiveFilter() const;
	std::string buildNegativeFilter() const;

	void generateOrderByClause( std::string& query ) const;
	void determineJoinsForQuery( std::string& query );
	void determineSelectClause( std::string& query, bool return_ids, bool return_hashes );
	void generateWhereClauses( std::string& query );
	/**
	 * @brief Constructs a query to be used. $1 is expected to be an array of tag_domain_ids
	 * @param return_ids
	 * @param return_hashes
	 * @param filter_domains
	 * @return
	 */
	std::string construct( bool return_ids = true, bool return_hashes = false, bool filter_domains = false );

  public:

	SearchBuilder();

	drogon::Task< drogon::orm::Result > query(
		DbClientPtr db,
		std::vector< TagDomainID > tag_domain_ids,
		bool return_ids = true,
		bool return_hashes = false );

	void setSortType( SortType type );

	void setSortOrder( SortOrder value );

	void filterTagDomain( TagDomainID value );

	void addFileDomain( FileDomainID value );
	drogon::Task< std::expected< void, std::shared_ptr< drogon::HttpResponse > > > setTags(
		const std::vector< std::string >& tags );

	void setPositiveTags( const std::vector< TagID >& vector );
	void setNegativeTags( const std::vector< TagID >& tag_ids );
	void setSystemTags( const std::vector< std::string >& vector );

	void setDisplay( HydrusDisplayType type );
};

} // namespace idhan
