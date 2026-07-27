//
// Created by kj16609 on 11/25/25.
//
#pragma once

#include <array>
#include <cstddef>
#include <expected>
#include <string_view>
#include <vector>

#include "ModuleBase.hpp"

struct archive; // libarchive opaque type

//! Canonical MIME types the archive modules handle.
const static std::vector< std::string_view > archive_handleable_mimes {
	"application/zip",
	"application/vnd.comicbook+zip"
};

//! \return The MIME types the archive modules handle (see archive_handleable_mimes).
[[nodiscard]] inline std::vector< std::string_view > getHandleableMimesForArchives()
{
	return archive_handleable_mimes;
}

//! Detects the character encoding of a NUL-terminated string using chardet.
//! \return The detected encoding name (e.g. "UTF-8", "SHIFT_JIS"), or a ModuleError on failure.
[[nodiscard]] std::expected< std::string, idhan::ModuleError > encoding( const char* str );

//! Detects a string's encoding (via encoding()) and transcodes it to UTF-8 with iconv.
//! Used to normalise archive member names, whose encoding is not guaranteed by the archive format.
//! \return The UTF-8 string, or a ModuleError if detection or conversion fails.
[[nodiscard]] std::expected< std::string, idhan::ModuleError > sanitizeEncoding( const char* str );

//! Reads all data for the archive's current entry into a buffer.
/** Loops over short reads and streams entries whose uncompressed size is not stored in the
 *  header, so callers never rely on archive_entry_size() for allocation.
 *
 *  Materialises the whole (decompressed) entry in memory. Only use this when the raw bytes are
 *  actually needed (e.g. extraction); callers that only need the digest must use
 *  hashArchiveEntryData(), which never holds more than one chunk at a time. */
[[nodiscard]] std::expected< std::vector< std::byte >, idhan::ModuleError > readArchiveEntryData( archive* a );

//! SHA-256 digest and uncompressed size of a single archive entry.
struct ArchiveEntryHash
{
	std::array< std::byte, ( 256 / 8 ) > m_hash {};
	std::size_t m_size { 0 };
};

//! Streams the archive's current entry through an incremental SHA-256, returning the digest and the
//! uncompressed byte count.
/** Unlike readArchiveEntryData() this never buffers the whole entry: it reads one fixed-size chunk
 *  at a time and folds each chunk into the running digest, so peak memory is O(chunk) regardless of
 *  the entry's uncompressed size. Use this whenever only the hash/size is needed (e.g. archive
 *  metadata), so a large member or a decompression bomb cannot inflate memory to the entry size. */
[[nodiscard]] std::expected< ArchiveEntryHash, idhan::ModuleError > hashArchiveEntryData( archive* a );

//! Converts a wide (wchar_t) archive path to UTF-8 via iconv (WCHAR_T -> UTF-8).
[[nodiscard]] std::expected< std::string, idhan::ModuleError > wideToUtf8( const wchar_t* str );