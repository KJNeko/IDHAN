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
/** libarchive's archive_read_open_memory wants the whole archive up front, which for a comic book
 *  archive means every page resident just to walk the member headers. This opens the same archive
 *  through a read callback instead, so only one chunk is in memory at a time.
 *
 *  The chunk is owned by this object and handed to libarchive by pointer, so an instance must
 *  outlive the archive handle it was opened on. Declare it before the handle and the ordinary
 *  reverse-order destruction gets that right.
 *
 *  Sequential only: libarchive reads forward and this never seeks, which is all the formats here
 *  need. */
class ArchiveModuleReader
{
	//! Matches the chunk size the entry-hashing loop already uses; big enough that the per-call
	//! overhead disappears, small enough that a decompression bomb cannot inflate memory.
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
/** The extraction counterpart to hashArchiveEntryData: one chunk at a time from libarchive straight
 *  into the sink, so an extracted member is written once into the memory that carries it to the
 *  host rather than being assembled in this process first. */
[[nodiscard]] std::expected< void, idhan::ModuleError > writeArchiveEntryData( archive* a, idhan::ModuleSink& out );

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

//! Outcome of asking an entry for its name.
enum class EntryNameResult
{
	OK, //!< A name was resolved.
	UNNAMED, //!< libarchive gave no name in any encoding; the entry has to be skipped.
	FAILED //!< A name exists but could not be transcoded to UTF-8.
};

//! The UTF-8 name of the archive's current entry.
/** Shared by the metadata and generator passes on purpose. The generator matches an entry name
 *  against a name the metadata pass recorded earlier, so the two must resolve identically -- two
 *  copies of this ladder are two chances for them to disagree, and a disagreement surfaces as a
 *  file that cannot be extracted rather than as an obvious bug.
 *
 *  UNNAMED usually means the process is in a locale whose charset cannot represent the name:
 *  archive_entry_pathname() converts through the C locale, so in "C" every non-ASCII name comes back
 *  null. Callers should skip the entry, not fail the archive -- an unreadable name says nothing
 *  about the other members. */
[[nodiscard]] EntryNameResult entryFilename(
	archive_entry* entry,
	std::expected< std::string, idhan::ModuleError >& out );
