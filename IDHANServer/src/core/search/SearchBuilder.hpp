//
// Created by kj16609 on 11/7/24.
//
#pragma once

#include <string_view>

#include "IDHANTypes.hpp"
#include "SearchBuilder.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#pragma GCC diagnostic ignored "-Wredundant-tags"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wnoexcept"
#pragma GCC diagnostic ignored "-Wredundant-decls"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wnoexcept"
#pragma GCC diagnostic ignored "-Wswitch-enum"
#pragma GCC diagnostic ignored "-Wshadow"
#include "drogon/HttpRequest.h"
#pragma GCC diagnostic pop

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
		case HY_FILESIZE:
			return SortType::HY_FILESIZE;
		case HY_DURATION:
			return SortType::HY_DURATION;
		case HY_IMPORT_TIME:
			return SortType::HY_IMPORT_TIME;
		case HY_MIME:
			return SortType::HY_MIME;
		case HY_RANDOM:
			return SortType::HY_RANDOM;
		case HY_WIDTH:
			return SortType::HY_WIDTH;
		case HY_HEIGHT:
			return SortType::HY_HEIGHT;
		case HY_RATIO:
			return SortType::HY_RATIO;
		case HY_NUM_PIXELS:
			return SortType::HY_NUM_PIXELS;
		case HY_NUM_TAGS:
			return SortType::HY_NUM_TAGS;
		case HY_MEDIA_VIEWS:
			return SortType::HY_MEDIA_VIEWS;
		case HY_MEDIA_VIEWTIME:
			return SortType::HY_MEDIA_VIEWTIME;
		case HY_APPROX_BITRATE:
			return SortType::HY_APPROX_BITRATE;
		case HY_HAS_AUDIO:
			return SortType::HY_HAS_AUDIO;
		case HY_FILE_MODIFIED_TIMESTAMP:
			return SortType::HY_FILE_MODIFIED_TIMESTAMP;
		case HY_FRAMERATE:
			return SortType::HY_FRAMERATE;
		case HY_NUM_FRAMES:
			return SortType::HY_NUM_FRAMES;
		case HY_NUM_COLLECTION_FILES:
			return SortType::HY_NUM_COLLECTION_FILES;
		case HY_LAST_VIEWED_TIME:
			return SortType::HY_LAST_VIEWED_TIME;
		case HY_ARCHIVED_TIMESTAMP:
			return SortType::HY_ARCHIVED_TIMESTAMP;
		case HY_HASH:
			return SortType::HY_HASH;
		case HY_PIXEL_HASH:
			return SortType::HY_PIXEL_HASH;
		case HY_BLURHASH:
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
	} m_required_joins {};

	SortType m_sort_type;
	SortOrder m_order;
	std::vector< TagID > m_tags;

	HydrusDisplayType m_display_mode;

  public:

	SearchBuilder();

	drogon::Task< drogon::orm::Result > query(
		drogon::orm::DbClientPtr db,
		std::vector< TagDomainID > tag_domain_ids,
		bool return_ids = true,
		bool return_hashes = false );

	drogon::Task< drogon::orm::Result >
		query( drogon::orm::DbClientPtr db, bool return_ids = true, bool return_hashes = false );

	/**
	 * @brief Constructs a query to be used. $1 is expected to be an array of tag_domain_ids
	 * @param return_ids
	 * @param return_hashes
	 * @return
	 */
	std::string construct( bool return_ids = true, bool return_hashes = false, bool filter_domains = false );
	void setSortType( SortType type );

	void setSortOrder( SortOrder value );

	void filterTagDomain( TagDomainID value );

	void addFileDomain( FileDomainID value );

	void setTags( const std::vector< TagID >& vector );

	void setDisplay( HydrusDisplayType type );
};

} // namespace idhan