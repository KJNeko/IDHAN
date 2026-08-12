#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ptr/flatten/DefinitionStore.hpp"
#include "ptr/flatten/TagUsageSet.hpp"

namespace idhan::hydrus::ptr
{

inline constexpr std::array< char, 8 > CHUNK_MAGIC { { 'I', 'D', 'H', 'A', 'N', 'P', 'T', 'C' } };
inline constexpr std::array< char, 8 > RELATIONS_MAGIC { { 'I', 'D', 'H', 'A', 'N', 'P', 'T', 'R' } };
//! Bumped to 2 when terminal-delete discarding and the tag accounting landed. Nothing reads a
//! version 1 directory: readChunk and isCompactedDirectory both reject an unrecognised version, so
//! an old compacted directory fails loudly rather than importing under assumptions that no longer
//! hold. Re-flattening the corpus is the migration.
inline constexpr std::uint32_t CHUNK_FORMAT_VERSION { 2 };

#pragma pack( push, 1 )

//! Uncompressed prefix of a chunk file. body_size lets a reader allocate exactly once before
//! inflating, rather than growing a buffer.
struct ChunkHeader
{
	std::array< char, 8 > magic;
	std::uint32_t version;
	std::uint64_t body_size;
	std::uint32_t record_count;
	std::uint32_t string_count;
};

#pragma pack( pop )

static_assert( sizeof( ChunkHeader ) == 28, "Chunk header layout is format" );

//! What writing one chunk produced.
struct ChunkStats
{
	std::uint64_t records { 0 };
	std::uint64_t mappings { 0 };
	std::uint64_t missing_definitions { 0 };
};

//! Resolves a PTR tag id to its text. Kept as a callable so ChunkWriter has no dependency on
//! where definitions actually live.
using TagLookup = std::function< std::optional< std::string_view >( std::uint32_t ) >;

//! Accumulates records in memory, then writes one compacted chunk.
//!
//! Records are added carrying PTR tag ids. finish() collects every distinct id, resolves it
//! through \p lookup, builds the string table sorted by ptr_tag_id, and rewrites each record's
//! ids as indices into that table. Sorting by id groups tags created in the same PTR era, which
//! share long prefixes and therefore compress well.
class ChunkWriter
{
  public:

	explicit ChunkWriter( std::filesystem::path path );

	ChunkWriter( const ChunkWriter& ) = delete;
	ChunkWriter& operator=( const ChunkWriter& ) = delete;
	ChunkWriter( ChunkWriter&& ) = delete;
	ChunkWriter& operator=( ChunkWriter&& ) = delete;

	~ChunkWriter();

	//! \param sha256 The record's binary hash.
	//! \param add_tag_ids PTR tag ids to apply. \param del_tag_ids PTR tag ids to remove.
	void addRecord( std::array< std::byte, SHA256_BYTES > sha256,
	                std::vector< std::uint32_t > add_tag_ids,
	                std::vector< std::uint32_t > del_tag_ids );

	std::size_t recordCount() const noexcept { return m_records.size(); }

	bool empty() const noexcept { return m_records.empty(); }

	const std::filesystem::path& path() const noexcept { return m_path; }

	//! Resolves, sorts, compresses and writes. Safe to call once; calling twice throws.
	//!
	//! \param usage Marked with every tag id that reaches this chunk's string table, or null to
	//!        track nothing. The string table is exactly the set of ids being written, so this
	//!        costs one pass over a list already in hand.
	ChunkStats finish( const TagLookup& lookup, TagUsageSet* usage = nullptr );

  private:

	struct PendingRecord
	{
		std::array< std::byte, SHA256_BYTES > sha256;
		std::vector< std::uint32_t > add_tag_ids;
		std::vector< std::uint32_t > del_tag_ids;
	};

	std::filesystem::path m_path;
	std::vector< PendingRecord > m_records {};
	bool m_finished { false };
};

//! One entry of a chunk's string table.
struct ChunkStringEntry
{
	std::uint32_t ptr_tag_id { 0 };
	std::string tag {};
};

//! One record as read back, with tag indices into Chunk::strings.
struct ChunkRecord
{
	std::array< std::byte, SHA256_BYTES > sha256 {};
	std::vector< std::uint32_t > add_indices {};
	std::vector< std::uint32_t > del_indices {};
};

//! A whole chunk, decompressed.
struct Chunk
{
	std::vector< ChunkStringEntry > strings {};
	std::vector< ChunkRecord > records {};
};

//! \throws std::runtime_error on bad magic, unknown version, or truncation.
Chunk readChunk( const std::filesystem::path& path );

} // namespace idhan::hydrus::ptr
