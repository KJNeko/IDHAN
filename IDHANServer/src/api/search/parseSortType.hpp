#pragma once

#include <string>

#include "core/search/SearchBuilder.hpp"

namespace idhan::api
{

//! Maps a "sort.by" string (POST /search JSON, GET /search query param) onto a SortType.
//! Unknown values fall back to import time.
inline SortType parseSortType( const std::string& by )
{
	if ( by == "filesize" || by == "size" ) return SortType::FILESIZE;
	// "creation_time" and "modified_time" predate the file/record split and are kept for old clients.
	if ( by == "record_creation_time" || by == "record_time" || by == "creation_time" )
		return SortType::RECORD_CREATION_TIME;
	if ( by == "file_modified_time" || by == "file_mtime" || by == "modified_time" )
		return SortType::FILE_MODIFIED_TIME;
	if ( by == "file_created_time" || by == "file_ctime" ) return SortType::FILE_CREATED_TIME;
	if ( by == "mime" || by == "filetype" ) return SortType::MIME;
	if ( by == "hash" ) return SortType::HASH;
	if ( by == "random" ) return SortType::RANDOM;
	if ( by == "duration" ) return SortType::DURATION;
	if ( by == "framerate" ) return SortType::FRAMERATE;
	if ( by == "has_audio" ) return SortType::HAS_AUDIO;
	if ( by == "width" ) return SortType::WIDTH;
	if ( by == "height" ) return SortType::HEIGHT;
	if ( by == "ratio" ) return SortType::RATIO;
	if ( by == "num_pixels" ) return SortType::NUM_PIXELS;
	if ( by == "num_tags" ) return SortType::NUM_TAGS;
	// "import_time" and anything unrecognised
	return SortType::IMPORT_TIME;
}

} // namespace idhan::api
