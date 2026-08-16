#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "ModuleBase.hpp"
#include "ModuleSink.hpp"

struct archive; // libarchive opaque type
struct archive_entry; // libarchive opaque type

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

//! Feeds a ModuleFile to libarchive without materialising it.
class ArchiveModuleReader
{
	static constexpr std::size_t CHUNK { 64 * 1024 };

	const idhan::ModuleFile* m_file { nullptr };
	std::vector< std::byte > m_chunk;
	std::size_t m_position { 0 };
	std::string m_error {};

	//! libarchive read callback. Returns bytes read, 0 at end of archive, or -1 on failure.
	static std::int64_t onRead( archive* a, void* client_data, const void** buffer );

  public:

	explicit ArchiveModuleReader( const idhan::ModuleFile& file ) : m_file( &file ), m_chunk( CHUNK ) {}

	ArchiveModuleReader( const ArchiveModuleReader& ) = delete;
	ArchiveModuleReader& operator=( const ArchiveModuleReader& ) = delete;
	ArchiveModuleReader( ArchiveModuleReader&& ) = delete;
	ArchiveModuleReader& operator=( ArchiveModuleReader&& ) = delete;

	//! Opens \p a on this file. The equivalent of archive_read_open_memory, minus the memory.
	[[nodiscard]] std::expected< void, idhan::ModuleError > open( archive* a );
};

//! Streams the archive's current entry into \p out.
[[nodiscard]] std::expected< void, idhan::ModuleError > writeArchiveEntryData( archive* a, idhan::ModuleSink& out );

//! Detects the character encoding of a NUL-terminated string using chardet.
//! \return The detected encoding name (e.g. "UTF-8", "SHIFT_JIS"), or a ModuleError on failure.
[[nodiscard]] std::expected< std::string, idhan::ModuleError > encoding( const char* str );

//! Detects a string's encoding (via encoding()) and transcodes it to UTF-8 with iconv.
//! Used to normalise archive member names, whose encoding is not guaranteed by the archive format.
//! \return The UTF-8 string, or a ModuleError if detection or conversion fails.
[[nodiscard]] std::expected< std::string, idhan::ModuleError > sanitizeEncoding( const char* str );

//! Reads all data for the archive's current entry into a buffer.
[[nodiscard]] std::expected< std::vector< std::byte >, idhan::ModuleError > readArchiveEntryData( archive* a );

//! SHA-256 digest and uncompressed size of a single archive entry.
struct ArchiveEntryHash
{
	std::array< std::byte, ( 256 / 8 ) > m_hash {};
	std::size_t m_size { 0 };
};

//! Streams the archive's current entry through an incremental SHA-256, returning the digest and the
//! uncompressed byte count.
[[nodiscard]] std::expected< ArchiveEntryHash, idhan::ModuleError > hashArchiveEntryData( archive* a );

//! Converts a wide (wchar_t) archive path to UTF-8 via iconv (WCHAR_T -> UTF-8).
[[nodiscard]] std::expected< std::string, idhan::ModuleError > wideToUtf8( const wchar_t* str );

//! Outcome of asking an entry for its name.
enum class EntryNameResult
{
	OK, //!< A name was resolved.
	UNNAMED, //!< libarchive gave no name in any encoding; the entry has to be skipped.
	FAILED //!< A name exists but could not be transcoded to UTF-8.
};

//! The UTF-8 name of the archive's current entry.
[[nodiscard]] EntryNameResult entryFilename(
	archive_entry* entry,
	std::expected< std::string, idhan::ModuleError >& out );
