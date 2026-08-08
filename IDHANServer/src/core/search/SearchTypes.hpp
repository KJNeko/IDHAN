//
// Created by kj16609 on 11/7/24.
//
// Sort/display enums shared by SearchBuilder and the Set machinery it drives. Split out of
// SearchBuilder.hpp so SortKey/Set can name SortType without including the builder that uses them.
#pragma once

#include "hydrus/ClientConstants_gen.hpp"

namespace idhan
{

//! Direction results are ordered in.
enum class SortOrder
{
	ASC,
	DESC,

	DEFAULT = ASC
};

//! Column/metric results are ordered by. The HY_* aliases map Hydrus sort types onto the subset
//! IDHAN currently implements (several collapse to DEFAULT until natively supported).
enum class SortType
{
	FILESIZE,
	IMPORT_TIME,
	RECORD_TIME,

	MODIFIED_TIME,
	MIME,
	HASH,
	RANDOM,
	DURATION,
	FRAMERATE,
	HAS_AUDIO,
	WIDTH,
	HEIGHT,
	RATIO,
	NUM_PIXELS,
	NUM_TAGS,

	DEFAULT = FILESIZE,

	// BEGIN_HYDRUS_CONVERT
	HY_FILESIZE = FILESIZE,
	HY_DURATION = DURATION,
	HY_IMPORT_TIME = IMPORT_TIME,
	HY_MIME = MIME,
	HY_RANDOM = RANDOM,
	HY_WIDTH = WIDTH,
	HY_HEIGHT = HEIGHT,
	HY_RATIO = RATIO,
	HY_NUM_PIXELS = NUM_PIXELS,
	HY_NUM_TAGS = NUM_TAGS,
	// no per-file view-tracking subsystem exists yet
	HY_MEDIA_VIEWS = DEFAULT,
	HY_MEDIA_VIEWTIME = DEFAULT,
	// Hydrus derives this from num_frames, which IDHAN doesn't store
	HY_APPROX_BITRATE = DEFAULT,
	HY_HAS_AUDIO = HAS_AUDIO,
	HY_FILE_MODIFIED_TIMESTAMP = MODIFIED_TIME,
	HY_FRAMERATE = FRAMERATE,
	// num_frames isn't captured at import time
	HY_NUM_FRAMES = DEFAULT,
	// IDHAN has no collections concept
	HY_NUM_COLLECTION_FILES = DEFAULT,
	// no per-file view-tracking subsystem exists yet
	HY_LAST_VIEWED_TIME = DEFAULT,
	// IDHAN's archive_map/archive_metadata are encrypted archive containers, unrelated to
	// Hydrus's inbox/archive review-state concept, which IDHAN doesn't implement
	HY_ARCHIVED_TIMESTAMP = DEFAULT,
	HY_HASH = HASH,
	// no pixel_hash column
	HY_PIXEL_HASH = DEFAULT,
	// blurhash isn't generated
	HY_BLURHASH = DEFAULT,
	// END_HYDRUS_CONVERT
};

//! Hydrus API sort-type constants, as received from Hydrus-compatible clients.
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

//! Which mapping layer a search runs against: raw STORED mappings, or the sibling/parent-resolved
//! DISPLAY mappings.
enum class HydrusDisplayType
{
	STORED,
	DISPLAY,

	DEFAULT = DISPLAY
};

//! Maps a Hydrus sort type onto the nearest IDHAN SortType (see SortType's HY_* aliases).
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

} // namespace idhan
