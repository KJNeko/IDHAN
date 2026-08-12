#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>

namespace idhan::hydrus::ptr
{

inline constexpr std::size_t SHA256_BYTES { 32 };

inline constexpr const char* HASHES_FILENAME { "hashes.bin" };
inline constexpr const char* TAG_INDEX_FILENAME { "tags.idx" };
inline constexpr const char* TAG_BLOB_FILENAME { "tags.blob" };

//! \return The 32 raw bytes of \p hex, or nullopt if it is not exactly 64 hex characters.
std::optional< std::array< std::byte, SHA256_BYTES > > decodeSha256Hex( std::string_view hex );

//! Writes the PTR id-to-value tables as flat, id-indexed sparse files.
//!
//! hashes.bin is a fixed 32-byte stride indexed by hash_id; an all-zero slot means absent.
//! tags.idx is a fixed 8-byte stride of (u32 offset, u32 length) into tags.blob; a zero length
//! means absent. Because the id is the offset, nothing is sorted and nothing is searched, and
//! the files are sparse so unused id ranges cost no blocks.
//!
//! writeHash and writeTag are thread-safe. Both address their output by id rather than appending,
//! so concurrent writers cannot interleave: the only shared state is the tags.blob allocation
//! cursor, and that is held just long enough to reserve a range -- the write itself happens outside
//! the lock.
//!
//! \note If two update files define one tag_id with different text, whichever index write lands
//!       last wins, so which is kept is not deterministic across runs. PTR ids are stable, so in
//!       practice the text is identical and the choice does not matter.
//!
//! \warning Construction TRUNCATES all three files, so only one writer may exist per directory
//!          per run. Constructing a second one discards everything the first wrote. A scan builds
//!          exactly one and keeps it for the whole corpus.
class DefinitionWriter
{
  public:

	//! \warning Truncates hashes.bin, tags.idx and tags.blob in \p dir. See the class note.
	explicit DefinitionWriter( const std::filesystem::path& dir );

	DefinitionWriter( const DefinitionWriter& ) = delete;
	DefinitionWriter& operator=( const DefinitionWriter& ) = delete;
	DefinitionWriter( DefinitionWriter&& ) = delete;
	DefinitionWriter& operator=( DefinitionWriter&& ) = delete;

	~DefinitionWriter();

	//! \param sha256_hex Must be exactly 64 hex characters. Anything else is rejected here so it
	//!        can never reach a chunk, generalising the length check the importer does today.
	//! \return true if written.
	bool writeHash( std::uint32_t hash_id, std::string_view sha256_hex );

	//! An empty tag is ignored: a zero length is how the index encodes "absent".
	void writeTag( std::uint32_t tag_id, std::string_view tag );

	std::uint64_t rejectedHashes() const noexcept { return m_rejected_hashes.load( std::memory_order_relaxed ); }

  private:

	int m_hashes_fd { -1 };
	int m_tag_index_fd { -1 };
	int m_tag_blob_fd { -1 };

	//! Guards m_blob_offset only. Held for the reservation, never for the write.
	std::mutex m_blob_mutex {};
	std::uint64_t m_blob_offset { 0 };

	std::atomic< std::uint64_t > m_rejected_hashes { 0 };
};

//! Read-only mmap over the files DefinitionWriter produced. Resident cost is page cache, which
//! the kernel reclaims under pressure, rather than heap the process must hold.
class DefinitionReader
{
  public:

	explicit DefinitionReader( const std::filesystem::path& dir );

	DefinitionReader( const DefinitionReader& ) = delete;
	DefinitionReader& operator=( const DefinitionReader& ) = delete;
	DefinitionReader( DefinitionReader&& ) = delete;
	DefinitionReader& operator=( DefinitionReader&& ) = delete;

	~DefinitionReader();

	//! \return A 32-byte view, or nullopt if \p hash_id was never defined.
	std::optional< std::span< const std::byte > > hash( std::uint32_t hash_id ) const;

	//! \return A view into the mmap, valid for this reader's lifetime, or nullopt if undefined.
	std::optional< std::string_view > tag( std::uint32_t tag_id ) const;

	//! One past the highest id tags.idx has a slot for. Sizes a TagUsageSet.
	std::uint32_t tagIdCapacity() const noexcept;

	//! How many tag ids the corpus actually defined. Counted once during construction by walking
	//! tags.idx for non-zero lengths; the file is sparse, so the id ranges nothing ever defined are
	//! holes that read as zeros without touching the disk.
	std::uint64_t definedTagCount() const noexcept { return m_defined_tags; }

  private:

	struct Mapping
	{
		const std::byte* data { nullptr };
		std::size_t size { 0 };
	};

	static Mapping mapFile( const std::filesystem::path& path );
	static void unmapFile( Mapping& mapping );

	//! \pre m_tag_index is mapped.
	std::uint64_t countDefinedTags() const noexcept;

	Mapping m_hashes {};
	Mapping m_tag_index {};
	Mapping m_tag_blob {};

	std::uint64_t m_defined_tags { 0 };
};

} // namespace idhan::hydrus::ptr
