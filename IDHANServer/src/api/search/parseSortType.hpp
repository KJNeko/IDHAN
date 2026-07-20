//
// Created by kj16609 on 7/20/26.
//
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
	if ( by == "record_time" || by == "creation_time" ) return SortType::RECORD_TIME;
	if ( by == "modified_time" ) return SortType::MODIFIED_TIME;
	if ( by == "mime" || by == "filetype" ) return SortType::MIME;
	if ( by == "hash" ) return SortType::HASH;
	// "import_time" and anything unrecognised
	return SortType::IMPORT_TIME;
}

} // namespace idhan::api
