//
// Created by kj16609 on 11/25/25.
//
#pragma once

#include <cstddef>
#include <expected>
#include <string_view>
#include <vector>

#include "ModuleBase.hpp"

struct archive; // libarchive opaque type

const static std::vector< std::string_view > archive_handleable_mimes {
	"application/zip",
	"application/vnd.comicbook+zip"
};

[[nodiscard]] inline std::vector< std::string_view > getHandleableMimesForArchives()
{
	return archive_handleable_mimes;
}

[[nodiscard]] std::expected< std::string, idhan::ModuleError > encoding( const char* str );

[[nodiscard]] std::expected< std::string, idhan::ModuleError > sanitizeEncoding( const char* str );

//! Reads all data for the archive's current entry into a buffer.
/** Loops over short reads and streams entries whose uncompressed size is not stored in the
 *  header, so callers never rely on archive_entry_size() for allocation. */
[[nodiscard]] std::expected< std::vector< std::byte >, idhan::ModuleError > readArchiveEntryData( archive* a );