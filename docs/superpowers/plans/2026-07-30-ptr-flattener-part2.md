# PTR Flattener Implementation Plan — Part 2 (Tasks 5-7)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax for tracking.

Continues `docs/superpowers/plans/2026-07-30-ptr-flattener.md`. The **Global Constraints** in that file apply to every task here.

---

### Task 5: Definition store (pwrite writer, mmap reader)

The id-to-value tables, on disk instead of on the heap. This is the piece that removes the current importer's 20 GB translation map. The id *is* the offset, so nothing is sorted and nothing is searched.

**Files:**
- Create: `tools/HydrusImporter/ptr-core/include/ptr/flatten/DefinitionStore.hpp`
- Create: `tools/HydrusImporter/ptr-core/src/DefinitionStore.cpp`
- Test: `tests/core/ptr/definitionStore.cpp`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `std::optional< std::array< std::byte, 32 > > decodeSha256Hex( std::string_view )`; `class DefinitionWriter` with `explicit DefinitionWriter( std::filesystem::path dir )`, `bool writeHash( std::uint32_t, std::string_view )`, `void writeTag( std::uint32_t, std::string_view )`, `std::uint64_t rejectedHashes() const noexcept`; `class DefinitionReader` with `explicit DefinitionReader( const std::filesystem::path& dir )`, `std::optional< std::span< const std::byte > > hash( std::uint32_t ) const`, `std::optional< std::string_view > tag( std::uint32_t ) const`. Constants `HASHES_FILENAME`, `TAG_INDEX_FILENAME`, `TAG_BLOB_FILENAME`, `SHA256_BYTES`.

- [ ] **Step 1: Write the failing test**

Create `tests/core/ptr/definitionStore.cpp`:

```cpp
//
// Created by kj16609 on 7/30/26.
//

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "ptr/flatten/DefinitionStore.hpp"

namespace
{

using namespace idhan::hydrus::ptr;

constexpr std::string_view HASH_A { "f69a2836e6ab4e089b9a6695c3d65d2da02f8d69737135e0cec45e173aaafdcd" };
constexpr std::string_view HASH_B { "158408609c78c06ed19884efaef1155fd545b000a907ec9ef9c90fa406b45efc" };

class DefinitionStoreTest : public ::testing::Test
{
  protected:

	void SetUp() override
	{
		m_dir = std::filesystem::temp_directory_path() / "ptr-defs-test";
		std::filesystem::remove_all( m_dir );
		std::filesystem::create_directories( m_dir );
	}

	void TearDown() override { std::filesystem::remove_all( m_dir ); }

	std::filesystem::path m_dir;
};

TEST( PTRDecodeSha256Hex, DecodesLowercase )
{
	const auto decoded = decodeSha256Hex( HASH_A );
	ASSERT_TRUE( decoded.has_value() );
	EXPECT_EQ( static_cast< unsigned >( ( *decoded )[ 0 ] ), 0xf6u );
	EXPECT_EQ( static_cast< unsigned >( ( *decoded )[ 31 ] ), 0xcdu );
}

TEST( PTRDecodeSha256Hex, DecodesUppercase )
{
	EXPECT_TRUE( decodeSha256Hex( "AABBCCDDEEFF00112233445566778899AABBCCDDEEFF00112233445566778899" ).has_value() );
}

TEST( PTRDecodeSha256Hex, RejectsWrongLength )
{
	EXPECT_FALSE( decodeSha256Hex( "abcd" ).has_value() );
	EXPECT_FALSE( decodeSha256Hex( std::string( 63, 'a' ) ).has_value() );
	EXPECT_FALSE( decodeSha256Hex( std::string( 65, 'a' ) ).has_value() );
	EXPECT_FALSE( decodeSha256Hex( "" ).has_value() );
}

TEST( PTRDecodeSha256Hex, RejectsNonHexCharacters )
{
	EXPECT_FALSE( decodeSha256Hex( std::string( 63, 'a' ) + "z" ).has_value() );
}

TEST_F( DefinitionStoreTest, RoundTripsHashesAtSparseIds )
{
	{
		DefinitionWriter writer { m_dir };
		EXPECT_TRUE( writer.writeHash( 0, HASH_A ) );
		EXPECT_TRUE( writer.writeHash( 194'644'713, HASH_B ) );
		EXPECT_EQ( writer.rejectedHashes(), 0u );
	}

	const DefinitionReader reader { m_dir };

	const auto first = reader.hash( 0 );
	ASSERT_TRUE( first.has_value() );
	ASSERT_EQ( first->size(), SHA256_BYTES );
	EXPECT_EQ( *first, std::span< const std::byte >( *decodeSha256Hex( HASH_A ) ) );

	const auto last = reader.hash( 194'644'713 );
	ASSERT_TRUE( last.has_value() );
	EXPECT_EQ( *last, std::span< const std::byte >( *decodeSha256Hex( HASH_B ) ) );
}

TEST_F( DefinitionStoreTest, UnwrittenHashIdIsAbsent )
{
	{
		DefinitionWriter writer { m_dir };
		writer.writeHash( 10, HASH_A );
	}

	const DefinitionReader reader { m_dir };
	EXPECT_FALSE( reader.hash( 9 ).has_value() );
	EXPECT_TRUE( reader.hash( 10 ).has_value() );
	EXPECT_FALSE( reader.hash( 11 ).has_value() );
	EXPECT_FALSE( reader.hash( 4'000'000'000u ).has_value() );
}

TEST_F( DefinitionStoreTest, MalformedHashIsRejectedAndCounted )
{
	{
		DefinitionWriter writer { m_dir };
		EXPECT_FALSE( writer.writeHash( 1, "not-a-hash" ) );
		EXPECT_FALSE( writer.writeHash( 2, std::string( 64, 'z' ) ) );
		EXPECT_EQ( writer.rejectedHashes(), 2u );
	}

	const DefinitionReader reader { m_dir };
	EXPECT_FALSE( reader.hash( 1 ).has_value() );
	EXPECT_FALSE( reader.hash( 2 ).has_value() );
}

TEST_F( DefinitionStoreTest, RoundTripsTags )
{
	{
		DefinitionWriter writer { m_dir };
		writer.writeTag( 0, "character:hatsune miku" );
		writer.writeTag( 47'174'184, "creator:someone" );
		writer.writeTag( 5, "solo" );
	}

	const DefinitionReader reader { m_dir };

	const auto first = reader.tag( 0 );
	ASSERT_TRUE( first.has_value() );
	EXPECT_EQ( *first, "character:hatsune miku" );

	const auto last = reader.tag( 47'174'184 );
	ASSERT_TRUE( last.has_value() );
	EXPECT_EQ( *last, "creator:someone" );

	const auto middle = reader.tag( 5 );
	ASSERT_TRUE( middle.has_value() );
	EXPECT_EQ( *middle, "solo" );
}

TEST_F( DefinitionStoreTest, UnwrittenTagIdIsAbsent )
{
	{
		DefinitionWriter writer { m_dir };
		writer.writeTag( 100, "solo" );
	}

	const DefinitionReader reader { m_dir };
	EXPECT_FALSE( reader.tag( 99 ).has_value() );
	EXPECT_TRUE( reader.tag( 100 ).has_value() );
	EXPECT_FALSE( reader.tag( 101 ).has_value() );
}

TEST_F( DefinitionStoreTest, LastWriteWinsForARepeatedId )
{
	// PTR definitions are immutable, but a re-sent definition must not corrupt the store.
	{
		DefinitionWriter writer { m_dir };
		writer.writeTag( 3, "first" );
		writer.writeTag( 3, "second" );
	}

	const DefinitionReader reader { m_dir };
	const auto tag = reader.tag( 3 );
	ASSERT_TRUE( tag.has_value() );
	EXPECT_EQ( *tag, "second" );
}

TEST_F( DefinitionStoreTest, EmptyStoreReadsAsAllAbsent )
{
	{
		const DefinitionWriter writer { m_dir };
	}

	const DefinitionReader reader { m_dir };
	EXPECT_FALSE( reader.hash( 0 ).has_value() );
	EXPECT_FALSE( reader.tag( 0 ).has_value() );
}

} // namespace
```

- [ ] **Step 2: Run it and verify it fails**

```bash
cmake --build build/debug --target IDHANCoreTests -j$(nproc)
```

Expected: FAIL, `fatal error: ptr/flatten/DefinitionStore.hpp: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `tools/HydrusImporter/ptr-core/include/ptr/flatten/DefinitionStore.hpp`:

```cpp
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
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
class DefinitionWriter
{
  public:

	explicit DefinitionWriter( std::filesystem::path dir );

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

	std::uint64_t rejectedHashes() const noexcept { return m_rejected_hashes; }

  private:

	int m_hashes_fd { -1 };
	int m_tag_index_fd { -1 };
	int m_tag_blob_fd { -1 };
	std::uint64_t m_blob_offset { 0 };
	std::uint64_t m_rejected_hashes { 0 };
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

  private:

	struct Mapping
	{
		const std::byte* data { nullptr };
		std::size_t size { 0 };
	};

	static Mapping mapFile( const std::filesystem::path& path );
	static void unmapFile( Mapping& mapping );

	Mapping m_hashes {};
	Mapping m_tag_index {};
	Mapping m_tag_blob {};
};

} // namespace idhan::hydrus::ptr
```

- [ ] **Step 4: Write the implementation**

Create `tools/HydrusImporter/ptr-core/src/DefinitionStore.cpp`:

```cpp
#include "ptr/flatten/DefinitionStore.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <spdlog/spdlog.h>

#include <cstring>
#include <format>
#include <limits>
#include <stdexcept>

namespace idhan::hydrus::ptr
{

namespace
{

#pragma pack( push, 1 )
//! One tags.idx slot. A zero length means the id was never defined.
struct TagIndexEntry
{
	std::uint32_t offset;
	std::uint32_t length;
};
#pragma pack( pop )

static_assert( sizeof( TagIndexEntry ) == 8, "tags.idx stride is format" );

int openForWrite( const std::filesystem::path& path )
{
	const int fd = ::open( path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644 );
	if ( fd < 0 ) throw std::runtime_error( std::format( "Failed to open {} for writing", path.string() ) );
	return fd;
}

void writeAt( const int fd, const void* const data, const std::size_t size, const std::uint64_t offset )
{
	std::size_t written { 0 };
	while ( written < size )
	{
		const auto result = ::pwrite(
			fd,
			static_cast< const std::byte* >( data ) + written,
			size - written,
			static_cast< off_t >( offset + written ) );
		if ( result <= 0 ) throw std::runtime_error( "pwrite failed while writing definitions" );
		written += static_cast< std::size_t >( result );
	}
}

std::optional< std::byte > hexNibble( const char c )
{
	if ( c >= '0' && c <= '9' ) return static_cast< std::byte >( c - '0' );
	if ( c >= 'a' && c <= 'f' ) return static_cast< std::byte >( c - 'a' + 10 );
	if ( c >= 'A' && c <= 'F' ) return static_cast< std::byte >( c - 'A' + 10 );
	return std::nullopt;
}

} // namespace

std::optional< std::array< std::byte, SHA256_BYTES > > decodeSha256Hex( const std::string_view hex )
{
	if ( hex.size() != SHA256_BYTES * 2 ) return std::nullopt;

	std::array< std::byte, SHA256_BYTES > out {};
	for ( std::size_t i = 0; i < SHA256_BYTES; ++i )
	{
		const auto high = hexNibble( hex[ i * 2 ] );
		const auto low = hexNibble( hex[ i * 2 + 1 ] );
		if ( !high.has_value() || !low.has_value() ) return std::nullopt;
		out[ i ] = static_cast< std::byte >( ( std::to_integer< unsigned >( *high ) << 4 )
			                                 | std::to_integer< unsigned >( *low ) );
	}
	return out;
}

DefinitionWriter::DefinitionWriter( std::filesystem::path dir )
{
	std::filesystem::create_directories( dir );
	m_hashes_fd = openForWrite( dir / HASHES_FILENAME );
	m_tag_index_fd = openForWrite( dir / TAG_INDEX_FILENAME );
	m_tag_blob_fd = openForWrite( dir / TAG_BLOB_FILENAME );
}

DefinitionWriter::~DefinitionWriter()
{
	if ( m_hashes_fd >= 0 ) ::close( m_hashes_fd );
	if ( m_tag_index_fd >= 0 ) ::close( m_tag_index_fd );
	if ( m_tag_blob_fd >= 0 ) ::close( m_tag_blob_fd );
}

bool DefinitionWriter::writeHash( const std::uint32_t hash_id, const std::string_view sha256_hex )
{
	const auto decoded = decodeSha256Hex( sha256_hex );
	if ( !decoded.has_value() )
	{
		spdlog::warn( "Rejecting malformed hash definition for hash_id={} ({} chars)", hash_id, sha256_hex.size() );
		++m_rejected_hashes;
		return false;
	}

	writeAt( m_hashes_fd, decoded->data(), SHA256_BYTES, static_cast< std::uint64_t >( hash_id ) * SHA256_BYTES );
	return true;
}

void DefinitionWriter::writeTag( const std::uint32_t tag_id, const std::string_view tag )
{
	if ( tag.empty() ) return;

	if ( m_blob_offset + tag.size() > std::numeric_limits< std::uint32_t >::max() )
		throw std::runtime_error( "tags.blob exceeded 4 GB; TagIndexEntry offsets are 32-bit" );

	writeAt( m_tag_blob_fd, tag.data(), tag.size(), m_blob_offset );

	const TagIndexEntry entry { static_cast< std::uint32_t >( m_blob_offset ),
		                        static_cast< std::uint32_t >( tag.size() ) };
	writeAt( m_tag_index_fd, &entry, sizeof( entry ), static_cast< std::uint64_t >( tag_id ) * sizeof( entry ) );

	m_blob_offset += tag.size();
}

DefinitionReader::Mapping DefinitionReader::mapFile( const std::filesystem::path& path )
{
	std::error_code ec;
	const auto size = std::filesystem::file_size( path, ec );
	if ( ec || size == 0 ) return {};

	const int fd = ::open( path.c_str(), O_RDONLY );
	if ( fd < 0 ) throw std::runtime_error( std::format( "Failed to open {} for reading", path.string() ) );

	void* const data = ::mmap( nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0 );
	::close( fd );

	if ( data == MAP_FAILED ) throw std::runtime_error( std::format( "Failed to mmap {}", path.string() ) );

	return Mapping { static_cast< const std::byte* >( data ), size };
}

void DefinitionReader::unmapFile( Mapping& mapping )
{
	if ( mapping.data != nullptr ) ::munmap( const_cast< std::byte* >( mapping.data ), mapping.size );
	mapping = {};
}

DefinitionReader::DefinitionReader( const std::filesystem::path& dir ) :
  m_hashes( mapFile( dir / HASHES_FILENAME ) ),
  m_tag_index( mapFile( dir / TAG_INDEX_FILENAME ) ),
  m_tag_blob( mapFile( dir / TAG_BLOB_FILENAME ) )
{}

DefinitionReader::~DefinitionReader()
{
	unmapFile( m_hashes );
	unmapFile( m_tag_index );
	unmapFile( m_tag_blob );
}

std::optional< std::span< const std::byte > > DefinitionReader::hash( const std::uint32_t hash_id ) const
{
	const auto offset = static_cast< std::uint64_t >( hash_id ) * SHA256_BYTES;
	if ( offset + SHA256_BYTES > m_hashes.size ) return std::nullopt;

	const std::span< const std::byte > slot { m_hashes.data + offset, SHA256_BYTES };

	// A sparse hole reads as zeros, which is how an undefined id is encoded. A real SHA-256 of
	// all zeros is not something PTR will ever contain.
	for ( const auto byte : slot )
	{
		if ( byte != std::byte { 0 } ) return slot;
	}
	return std::nullopt;
}

std::optional< std::string_view > DefinitionReader::tag( const std::uint32_t tag_id ) const
{
	const auto offset = static_cast< std::uint64_t >( tag_id ) * sizeof( TagIndexEntry );
	if ( offset + sizeof( TagIndexEntry ) > m_tag_index.size ) return std::nullopt;

	TagIndexEntry entry {};
	std::memcpy( &entry, m_tag_index.data + offset, sizeof( entry ) );

	if ( entry.length == 0 ) return std::nullopt;
	if ( static_cast< std::uint64_t >( entry.offset ) + entry.length > m_tag_blob.size ) return std::nullopt;

	return std::string_view( reinterpret_cast< const char* >( m_tag_blob.data + entry.offset ), entry.length );
}

} // namespace idhan::hydrus::ptr
```

- [ ] **Step 5: Run the tests and verify they pass**

```bash
cmake --build build/debug --target IDHANCoreTests -j$(nproc)
./build/debug/bin/IDHANCoreTests --gtest_filter='PTRDecodeSha256Hex.*:DefinitionStoreTest.*'
```

Expected: 14 tests, all PASS.

- [ ] **Step 6: Confirm the sparse files are actually sparse**

```bash
./build/debug/bin/IDHANCoreTests --gtest_filter='DefinitionStoreTest.RoundTripsHashesAtSparseIds'
```

The test writes hash id 194,644,713, implying a 6.2 GB logical `hashes.bin`. It must pass in well under a second and must not fill the disk. If it is slow or consumes 6 GB, the filesystem is not honouring sparse writes — record that in the task notes, because the flattener's disk estimate depends on it.

- [ ] **Step 7: Commit**

```bash
git add tools/HydrusImporter/ptr-core/include/ptr/flatten/DefinitionStore.hpp tools/HydrusImporter/ptr-core/src/DefinitionStore.cpp tests/core/ptr/definitionStore.cpp
git commit -m "feat: add on-disk PTR definition store with mmap reader

Flat id-indexed sparse files replace the in-RAM hash_id and tag_id tables,
which at PTR scale need over 20 GB resident. The id is the offset, so nothing
is sorted or searched and unused id ranges cost no blocks.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 6: Chunk format writer and reader

The compacted output unit. Records carry PTR tag ids while being accumulated; `finish` resolves them through a caller-supplied lookup, builds the sorted string table, and remaps every record's indices in one pass.

**Files:**
- Create: `tools/HydrusImporter/ptr-core/include/ptr/flatten/ChunkFormat.hpp`
- Create: `tools/HydrusImporter/ptr-core/src/ChunkFormat.cpp`
- Test: `tests/core/ptr/chunkFormat.cpp`

**Interfaces:**
- Consumes: `SHA256_BYTES` from Task 5.
- Produces: `struct ChunkHeader`; `CHUNK_MAGIC`, `RELATIONS_MAGIC`, `CHUNK_FORMAT_VERSION`; `struct ChunkStats { std::uint64_t records, mappings, missing_definitions; }`; `using TagLookup = std::function< std::optional< std::string_view >( std::uint32_t ) >`; `class ChunkWriter`; `struct ChunkStringEntry`, `struct ChunkRecord`, `struct Chunk`; `Chunk readChunk( const std::filesystem::path& )`.

- [ ] **Step 1: Write the failing test**

Create `tests/core/ptr/chunkFormat.cpp`:

```cpp
//
// Created by kj16609 on 7/30/26.
//

#include <gtest/gtest.h>

#include <filesystem>
#include <map>
#include <string>

#include "ptr/flatten/ChunkFormat.hpp"

namespace
{

using namespace idhan::hydrus::ptr;

std::array< std::byte, SHA256_BYTES > sha( const unsigned char fill )
{
	std::array< std::byte, SHA256_BYTES > out {};
	out.fill( static_cast< std::byte >( fill ) );
	return out;
}

//! A lookup over a fixed table, so the writer can be tested with no DefinitionStore on disk.
TagLookup lookupOver( const std::map< std::uint32_t, std::string >& table )
{
	return [ &table ]( const std::uint32_t tag_id ) -> std::optional< std::string_view >
	{
		const auto it = table.find( tag_id );
		if ( it == table.end() ) return std::nullopt;
		return std::string_view( it->second );
	};
}

class ChunkFormatTest : public ::testing::Test
{
  protected:

	void SetUp() override
	{
		m_dir = std::filesystem::temp_directory_path() / "ptr-chunk-test";
		std::filesystem::remove_all( m_dir );
		std::filesystem::create_directories( m_dir );
		m_path = m_dir / "chunk-0000-0.idhanptr";
	}

	void TearDown() override { std::filesystem::remove_all( m_dir ); }

	std::filesystem::path m_dir;
	std::filesystem::path m_path;
};

TEST( PTRChunkHeader, IsTwentyEightBytes )
{
	EXPECT_EQ( sizeof( ChunkHeader ), 28u );
}

TEST_F( ChunkFormatTest, RoundTripsOneRecord )
{
	const std::map< std::uint32_t, std::string > tags { { 10, "solo" }, { 20, "character:miku" } };

	{
		ChunkWriter writer { m_path };
		writer.addRecord( sha( 0xAB ), { 10, 20 }, {} );
		EXPECT_EQ( writer.recordCount(), 1u );

		const auto stats = writer.finish( lookupOver( tags ) );
		EXPECT_EQ( stats.records, 1u );
		EXPECT_EQ( stats.mappings, 2u );
		EXPECT_EQ( stats.missing_definitions, 0u );
	}

	const auto chunk = readChunk( m_path );
	ASSERT_EQ( chunk.records.size(), 1u );
	ASSERT_EQ( chunk.strings.size(), 2u );

	EXPECT_EQ( chunk.records[ 0 ].sha256, sha( 0xAB ) );
	ASSERT_EQ( chunk.records[ 0 ].add_indices.size(), 2u );
	EXPECT_TRUE( chunk.records[ 0 ].del_indices.empty() );

	// The string table is sorted by ptr_tag_id, so index 0 is tag 10.
	EXPECT_EQ( chunk.strings[ 0 ].ptr_tag_id, 10u );
	EXPECT_EQ( chunk.strings[ 0 ].tag, "solo" );
	EXPECT_EQ( chunk.strings[ 1 ].ptr_tag_id, 20u );
	EXPECT_EQ( chunk.strings[ 1 ].tag, "character:miku" );

	EXPECT_EQ( chunk.strings[ chunk.records[ 0 ].add_indices[ 0 ] ].tag, "solo" );
	EXPECT_EQ( chunk.strings[ chunk.records[ 0 ].add_indices[ 1 ] ].tag, "character:miku" );
}

TEST_F( ChunkFormatTest, StringTableIsSortedByPtrTagIdRegardlessOfInsertionOrder )
{
	const std::map< std::uint32_t, std::string > tags { { 5, "e" }, { 1, "a" }, { 9, "i" } };

	{
		ChunkWriter writer { m_path };
		writer.addRecord( sha( 1 ), { 9, 5 }, { 1 } );
		writer.finish( lookupOver( tags ) );
	}

	const auto chunk = readChunk( m_path );
	ASSERT_EQ( chunk.strings.size(), 3u );
	EXPECT_EQ( chunk.strings[ 0 ].ptr_tag_id, 1u );
	EXPECT_EQ( chunk.strings[ 1 ].ptr_tag_id, 5u );
	EXPECT_EQ( chunk.strings[ 2 ].ptr_tag_id, 9u );

	ASSERT_EQ( chunk.records[ 0 ].add_indices.size(), 2u );
	EXPECT_EQ( chunk.strings[ chunk.records[ 0 ].add_indices[ 0 ] ].tag, "i" );
	EXPECT_EQ( chunk.strings[ chunk.records[ 0 ].add_indices[ 1 ] ].tag, "e" );
	ASSERT_EQ( chunk.records[ 0 ].del_indices.size(), 1u );
	EXPECT_EQ( chunk.strings[ chunk.records[ 0 ].del_indices[ 0 ] ].tag, "a" );
}

TEST_F( ChunkFormatTest, TagsAreInternedOnceAcrossRecords )
{
	const std::map< std::uint32_t, std::string > tags { { 7, "shared" } };

	{
		ChunkWriter writer { m_path };
		writer.addRecord( sha( 1 ), { 7 }, {} );
		writer.addRecord( sha( 2 ), { 7 }, {} );
		writer.addRecord( sha( 3 ), {}, { 7 } );
		writer.finish( lookupOver( tags ) );
	}

	const auto chunk = readChunk( m_path );
	EXPECT_EQ( chunk.strings.size(), 1u );
	EXPECT_EQ( chunk.records.size(), 3u );
}

TEST_F( ChunkFormatTest, MissingDefinitionsAreDroppedAndCounted )
{
	const std::map< std::uint32_t, std::string > tags { { 1, "known" } };

	{
		ChunkWriter writer { m_path };
		writer.addRecord( sha( 1 ), { 1, 999 }, { 998 } );
		const auto stats = writer.finish( lookupOver( tags ) );
		EXPECT_EQ( stats.mappings, 1u );
		EXPECT_EQ( stats.missing_definitions, 2u );
	}

	const auto chunk = readChunk( m_path );
	ASSERT_EQ( chunk.records.size(), 1u );
	ASSERT_EQ( chunk.strings.size(), 1u );
	EXPECT_EQ( chunk.records[ 0 ].add_indices.size(), 1u );
	EXPECT_TRUE( chunk.records[ 0 ].del_indices.empty() );
}

TEST_F( ChunkFormatTest, RecordWithNoResolvableTagsIsStillWritten )
{
	// The record hash is real PTR data even when every tag on it is undefined; dropping the
	// record would silently lose a file the corpus knows about.
	const std::map< std::uint32_t, std::string > tags {};

	{
		ChunkWriter writer { m_path };
		writer.addRecord( sha( 4 ), { 42 }, {} );
		writer.finish( lookupOver( tags ) );
	}

	const auto chunk = readChunk( m_path );
	ASSERT_EQ( chunk.records.size(), 1u );
	EXPECT_TRUE( chunk.records[ 0 ].add_indices.empty() );
	EXPECT_TRUE( chunk.strings.empty() );
}

TEST_F( ChunkFormatTest, RoundTripsAnEmptyChunk )
{
	{
		ChunkWriter writer { m_path };
		EXPECT_TRUE( writer.empty() );
		const auto stats = writer.finish( lookupOver( {} ) );
		EXPECT_EQ( stats.records, 0u );
	}

	const auto chunk = readChunk( m_path );
	EXPECT_TRUE( chunk.records.empty() );
	EXPECT_TRUE( chunk.strings.empty() );
}

TEST_F( ChunkFormatTest, RejectsAFileWithTheWrongMagic )
{
	{
		std::ofstream file { m_path, std::ios::binary };
		file << "NOTACHUNKNOTACHUNKNOTACHUNK!";
	}

	EXPECT_THROW( ( void ) readChunk( m_path ), std::runtime_error );
}

TEST_F( ChunkFormatTest, RoundTripsManyRecords )
{
	std::map< std::uint32_t, std::string > tags;
	for ( std::uint32_t i = 0; i < 200; ++i ) tags[ i ] = "tag:" + std::to_string( i );

	{
		ChunkWriter writer { m_path };
		for ( std::uint32_t r = 0; r < 1000; ++r )
			writer.addRecord( sha( static_cast< unsigned char >( r % 256 ) ), { r % 200, ( r + 1 ) % 200 }, {} );
		const auto stats = writer.finish( lookupOver( tags ) );
		EXPECT_EQ( stats.records, 1000u );
		EXPECT_EQ( stats.mappings, 2000u );
	}

	const auto chunk = readChunk( m_path );
	EXPECT_EQ( chunk.records.size(), 1000u );
	EXPECT_EQ( chunk.strings.size(), 200u );
	for ( const auto& record : chunk.records ) EXPECT_EQ( record.add_indices.size(), 2u );
}

} // namespace
```

Add `#include <fstream>` to the test's include block for `RejectsAFileWithTheWrongMagic`.

- [ ] **Step 2: Run it and verify it fails**

```bash
cmake --build build/debug --target IDHANCoreTests -j$(nproc)
```

Expected: FAIL, `fatal error: ptr/flatten/ChunkFormat.hpp: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `tools/HydrusImporter/ptr-core/include/ptr/flatten/ChunkFormat.hpp`:

```cpp
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ptr/flatten/DefinitionStore.hpp"

namespace idhan::hydrus::ptr
{

inline constexpr std::array< char, 8 > CHUNK_MAGIC { 'I', 'D', 'H', 'A', 'N', 'P', 'T', 'C' };
inline constexpr std::array< char, 8 > RELATIONS_MAGIC { 'I', 'D', 'H', 'A', 'N', 'P', 'T', 'R' };
inline constexpr std::uint32_t CHUNK_FORMAT_VERSION { 1 };

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

	//! \param sha256 Exactly SHA256_BYTES of binary hash.
	//! \param add_tag_ids PTR tag ids to apply. \param del_tag_ids PTR tag ids to remove.
	void addRecord( std::array< std::byte, SHA256_BYTES > sha256,
	                std::vector< std::uint32_t > add_tag_ids,
	                std::vector< std::uint32_t > del_tag_ids );

	std::size_t recordCount() const noexcept { return m_records.size(); }

	bool empty() const noexcept { return m_records.empty(); }

	const std::filesystem::path& path() const noexcept { return m_path; }

	//! Resolves, sorts, compresses and writes. Safe to call once; calling twice throws.
	ChunkStats finish( const TagLookup& lookup );

  private:

	struct PendingRecord
	{
		std::array< std::byte, SHA256_BYTES > sha256;
		std::vector< std::uint32_t > add_tag_ids;
		std::vector< std::uint32_t > del_tag_ids;
	};

	std::filesystem::path m_path;
	std::vector< PendingRecord > m_records;
	bool m_finished { false };
};

//! One entry of a chunk's string table.
struct ChunkStringEntry
{
	std::uint32_t ptr_tag_id;
	std::string tag;
};

//! One record as read back, with tag indices into Chunk::strings.
struct ChunkRecord
{
	std::array< std::byte, SHA256_BYTES > sha256;
	std::vector< std::uint32_t > add_indices;
	std::vector< std::uint32_t > del_indices;
};

//! A whole chunk, decompressed.
struct Chunk
{
	std::vector< ChunkStringEntry > strings;
	std::vector< ChunkRecord > records;
};

//! \throws std::runtime_error on bad magic, unknown version, or truncation.
Chunk readChunk( const std::filesystem::path& path );

} // namespace idhan::hydrus::ptr
```

- [ ] **Step 4: Write the implementation**

Create `tools/HydrusImporter/ptr-core/src/ChunkFormat.cpp`:

```cpp
#include "ptr/flatten/ChunkFormat.hpp"

#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <fstream>
#include <stdexcept>
#include <unordered_map>

namespace idhan::hydrus::ptr
{

namespace
{

template < typename T >
void appendPod( std::vector< std::byte >& out, const T& value )
{
	const auto* const bytes = reinterpret_cast< const std::byte* >( &value );
	out.insert( out.end(), bytes, bytes + sizeof( T ) );
}

//! Reads a POD from \p body at \p offset, advancing it. Throws rather than reading past the end.
template < typename T >
T takePod( const std::vector< std::byte >& body, std::size_t& offset, const char* const what )
{
	if ( offset + sizeof( T ) > body.size() )
		throw std::runtime_error( std::format( "Chunk truncated while reading {}", what ) );

	T value {};
	std::memcpy( &value, body.data() + offset, sizeof( T ) );
	offset += sizeof( T );
	return value;
}

std::vector< std::byte > deflateBuffer( const std::vector< std::byte >& input )
{
	uLongf bound = ::compressBound( static_cast< uLong >( input.size() ) );
	std::vector< std::byte > out( bound );

	const auto result = ::compress2(
		reinterpret_cast< Bytef* >( out.data() ),
		&bound,
		reinterpret_cast< const Bytef* >( input.data() ),
		static_cast< uLong >( input.size() ),
		Z_DEFAULT_COMPRESSION );

	if ( result != Z_OK ) throw std::runtime_error( "zlib compress2 failed while writing a chunk" );

	out.resize( bound );
	return out;
}

std::vector< std::byte > inflateBuffer( const std::vector< std::byte >& input, const std::uint64_t expected_size )
{
	std::vector< std::byte > out( expected_size );
	if ( expected_size == 0 ) return out;

	uLongf produced = static_cast< uLongf >( expected_size );
	const auto result = ::uncompress(
		reinterpret_cast< Bytef* >( out.data() ),
		&produced,
		reinterpret_cast< const Bytef* >( input.data() ),
		static_cast< uLong >( input.size() ) );

	if ( result != Z_OK ) throw std::runtime_error( "zlib uncompress failed while reading a chunk" );
	if ( produced != expected_size )
		throw std::runtime_error(
			std::format( "Chunk body inflated to {} bytes, header said {}", produced, expected_size ) );

	return out;
}

} // namespace

ChunkWriter::ChunkWriter( std::filesystem::path path ) : m_path( std::move( path ) ) {}

ChunkWriter::~ChunkWriter() = default;

void ChunkWriter::addRecord( std::array< std::byte, SHA256_BYTES > sha256,
                             std::vector< std::uint32_t > add_tag_ids,
                             std::vector< std::uint32_t > del_tag_ids )
{
	m_records.push_back( PendingRecord { sha256, std::move( add_tag_ids ), std::move( del_tag_ids ) } );
}

ChunkStats ChunkWriter::finish( const TagLookup& lookup )
{
	if ( m_finished ) throw std::runtime_error( std::format( "ChunkWriter::finish called twice for {}", m_path.string() ) );
	m_finished = true;

	ChunkStats stats {};
	stats.records = m_records.size();

	// Collect every distinct referenced id, then resolve once per id rather than once per use.
	std::vector< std::uint32_t > distinct;
	for ( const auto& record : m_records )
	{
		distinct.insert( distinct.end(), record.add_tag_ids.begin(), record.add_tag_ids.end() );
		distinct.insert( distinct.end(), record.del_tag_ids.begin(), record.del_tag_ids.end() );
	}
	std::ranges::sort( distinct );
	distinct.erase( std::ranges::unique( distinct ).begin(), distinct.end() );

	// distinct is already ascending, so the table it produces is sorted by ptr_tag_id.
	std::vector< ChunkStringEntry > strings;
	std::unordered_map< std::uint32_t, std::uint32_t > id_to_index;
	strings.reserve( distinct.size() );
	id_to_index.reserve( distinct.size() );

	for ( const auto tag_id : distinct )
	{
		const auto text = lookup( tag_id );
		if ( !text.has_value() ) continue;

		id_to_index.emplace( tag_id, static_cast< std::uint32_t >( strings.size() ) );
		strings.push_back( ChunkStringEntry { tag_id, std::string( *text ) } );
	}

	const auto remap = [ & ]( const std::vector< std::uint32_t >& ids )
	{
		std::vector< std::uint32_t > out;
		out.reserve( ids.size() );
		for ( const auto id : ids )
		{
			const auto it = id_to_index.find( id );
			if ( it == id_to_index.end() )
			{
				++stats.missing_definitions;
				continue;
			}
			out.push_back( it->second );
			++stats.mappings;
		}
		return out;
	};

	std::vector< std::byte > body;
	body.reserve( m_records.size() * 96 );

	for ( const auto& entry : strings )
	{
		appendPod( body, entry.ptr_tag_id );
		appendPod( body, static_cast< std::uint32_t >( entry.tag.size() ) );
		const auto* const bytes = reinterpret_cast< const std::byte* >( entry.tag.data() );
		body.insert( body.end(), bytes, bytes + entry.tag.size() );
	}

	for ( const auto& record : m_records )
	{
		const auto adds = remap( record.add_tag_ids );
		const auto dels = remap( record.del_tag_ids );

		body.insert( body.end(), record.sha256.begin(), record.sha256.end() );
		appendPod( body, static_cast< std::uint32_t >( adds.size() ) );
		appendPod( body, static_cast< std::uint32_t >( dels.size() ) );
		for ( const auto index : adds ) appendPod( body, index );
		for ( const auto index : dels ) appendPod( body, index );
	}

	ChunkHeader header {};
	header.magic = CHUNK_MAGIC;
	header.version = CHUNK_FORMAT_VERSION;
	header.body_size = body.size();
	header.record_count = static_cast< std::uint32_t >( m_records.size() );
	header.string_count = static_cast< std::uint32_t >( strings.size() );

	const auto compressed = deflateBuffer( body );

	std::ofstream file { m_path, std::ios::binary | std::ios::trunc };
	if ( !file ) throw std::runtime_error( std::format( "Failed to open chunk {} for writing", m_path.string() ) );

	file.write( reinterpret_cast< const char* >( &header ), sizeof( header ) );
	file.write( reinterpret_cast< const char* >( compressed.data() ), static_cast< std::streamsize >( compressed.size() ) );
	if ( !file ) throw std::runtime_error( std::format( "Failed to write chunk {}", m_path.string() ) );

	return stats;
}

Chunk readChunk( const std::filesystem::path& path )
{
	std::ifstream file { path, std::ios::binary | std::ios::ate };
	if ( !file ) throw std::runtime_error( std::format( "Failed to open chunk {}", path.string() ) );

	const auto file_size = static_cast< std::uint64_t >( file.tellg() );
	if ( file_size < sizeof( ChunkHeader ) )
		throw std::runtime_error( std::format( "Chunk {} is too small to hold a header", path.string() ) );

	file.seekg( 0 );

	ChunkHeader header {};
	file.read( reinterpret_cast< char* >( &header ), sizeof( header ) );

	if ( header.magic != CHUNK_MAGIC )
		throw std::runtime_error( std::format( "Chunk {} has the wrong magic", path.string() ) );
	if ( header.version != CHUNK_FORMAT_VERSION )
		throw std::runtime_error(
			std::format( "Chunk {} is format version {}, expected {}", path.string(), header.version, CHUNK_FORMAT_VERSION ) );

	std::vector< std::byte > compressed( file_size - sizeof( ChunkHeader ) );
	if ( !compressed.empty() )
		file.read( reinterpret_cast< char* >( compressed.data() ), static_cast< std::streamsize >( compressed.size() ) );

	const auto body = inflateBuffer( compressed, header.body_size );

	Chunk chunk {};
	std::size_t offset { 0 };

	chunk.strings.reserve( header.string_count );
	for ( std::uint32_t i = 0; i < header.string_count; ++i )
	{
		ChunkStringEntry entry {};
		entry.ptr_tag_id = takePod< std::uint32_t >( body, offset, "a string table id" );
		const auto length = takePod< std::uint32_t >( body, offset, "a string table length" );

		if ( offset + length > body.size() ) throw std::runtime_error( "Chunk truncated inside the string table" );
		entry.tag.assign( reinterpret_cast< const char* >( body.data() + offset ), length );
		offset += length;

		chunk.strings.push_back( std::move( entry ) );
	}

	const auto takeIndices = [ & ]( const std::uint32_t count )
	{
		std::vector< std::uint32_t > out;
		out.reserve( count );
		for ( std::uint32_t i = 0; i < count; ++i ) out.push_back( takePod< std::uint32_t >( body, offset, "a tag index" ) );
		return out;
	};

	chunk.records.reserve( header.record_count );
	for ( std::uint32_t i = 0; i < header.record_count; ++i )
	{
		ChunkRecord record {};

		if ( offset + SHA256_BYTES > body.size() ) throw std::runtime_error( "Chunk truncated inside a record hash" );
		std::memcpy( record.sha256.data(), body.data() + offset, SHA256_BYTES );
		offset += SHA256_BYTES;

		const auto add_count = takePod< std::uint32_t >( body, offset, "an add count" );
		const auto del_count = takePod< std::uint32_t >( body, offset, "a delete count" );

		record.add_indices = takeIndices( add_count );
		record.del_indices = takeIndices( del_count );

		chunk.records.push_back( std::move( record ) );
	}

	return chunk;
}

} // namespace idhan::hydrus::ptr
```

- [ ] **Step 5: Run the tests and verify they pass**

```bash
cmake --build build/debug --target IDHANCoreTests -j$(nproc)
./build/debug/bin/IDHANCoreTests --gtest_filter='PTRChunkHeader.*:ChunkFormatTest.*'
```

Expected: 9 tests, all PASS.

- [ ] **Step 6: Commit**

```bash
git add tools/HydrusImporter/ptr-core/include/ptr/flatten/ChunkFormat.hpp tools/HydrusImporter/ptr-core/src/ChunkFormat.cpp tests/core/ptr/chunkFormat.cpp
git commit -m "feat: add compacted chunk format writer and reader

Self-contained record-major chunk: uncompressed header, zlib body holding a
string table sorted by PTR tag id followed by records. Each string entry
carries its PTR tag id so the importer can create each tag exactly once
without holding tag text on the heap.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 7: Manifest read and write

The file whose presence marks a directory as compacted. Written last, so a cancelled or crashed flatten cannot be half-imported.

**Files:**
- Create: `tools/HydrusImporter/ptr-core/include/ptr/flatten/Manifest.hpp`
- Create: `tools/HydrusImporter/ptr-core/src/Manifest.cpp`
- Test: `tests/core/ptr/manifest.cpp`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `struct ChunkEntry { std::string file; std::uint64_t records, mappings; }`; `struct FlattenStats { std::uint64_t events_scanned, mappings_after_collapse, events_collapsed, terminal_deletes, skipped_files, skipped_missing_definitions; }`; `struct CompactManifest`; `void writeManifest( const std::filesystem::path& dir, const CompactManifest& )`; `CompactManifest readManifest( const std::filesystem::path& dir )`; `bool isCompactedDirectory( const std::filesystem::path& dir )`; constant `MANIFEST_FILENAME`.

- [ ] **Step 1: Write the failing test**

Create `tests/core/ptr/manifest.cpp`:

```cpp
//
// Created by kj16609 on 7/30/26.
//

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "ptr/flatten/Manifest.hpp"

namespace
{

using namespace idhan::hydrus::ptr;

class ManifestTest : public ::testing::Test
{
  protected:

	void SetUp() override
	{
		m_dir = std::filesystem::temp_directory_path() / "ptr-manifest-test";
		std::filesystem::remove_all( m_dir );
		std::filesystem::create_directories( m_dir );
	}

	void TearDown() override { std::filesystem::remove_all( m_dir ); }

	CompactManifest sample() const
	{
		CompactManifest manifest {};
		manifest.first_update_index = 0;
		manifest.last_update_index = 2599;
		manifest.max_records_per_chunk = 200'000;
		manifest.relations_file = "relations.idhanptr";
		manifest.chunks.push_back( ChunkEntry { "chunk-0000-0.idhanptr", 200'000, 3'800'000 } );
		manifest.chunks.push_back( ChunkEntry { "chunk-0001-0.idhanptr", 12'345, 210'000 } );
		manifest.stats.events_scanned = 3'200'000'000;
		manifest.stats.mappings_after_collapse = 2'900'000'000;
		manifest.stats.events_collapsed = 300'000'000;
		manifest.stats.terminal_deletes = 1'500'000;
		manifest.stats.skipped_files = 3;
		manifest.stats.skipped_missing_definitions = 91;
		return manifest;
	}

	std::filesystem::path m_dir;
};

TEST_F( ManifestTest, RoundTrips )
{
	writeManifest( m_dir, sample() );
	const auto read = readManifest( m_dir );

	EXPECT_EQ( read.format_version, CHUNK_FORMAT_VERSION );
	EXPECT_EQ( read.first_update_index, 0 );
	EXPECT_EQ( read.last_update_index, 2599 );
	EXPECT_EQ( read.max_records_per_chunk, 200'000u );
	EXPECT_EQ( read.relations_file, "relations.idhanptr" );

	ASSERT_EQ( read.chunks.size(), 2u );
	EXPECT_EQ( read.chunks[ 0 ].file, "chunk-0000-0.idhanptr" );
	EXPECT_EQ( read.chunks[ 0 ].records, 200'000u );
	EXPECT_EQ( read.chunks[ 0 ].mappings, 3'800'000u );
	EXPECT_EQ( read.chunks[ 1 ].records, 12'345u );

	// The counters exceed 32 bits; they must survive as 64-bit values.
	EXPECT_EQ( read.stats.events_scanned, 3'200'000'000u );
	EXPECT_EQ( read.stats.mappings_after_collapse, 2'900'000'000u );
	EXPECT_EQ( read.stats.events_collapsed, 300'000'000u );
	EXPECT_EQ( read.stats.terminal_deletes, 1'500'000u );
	EXPECT_EQ( read.stats.skipped_files, 3u );
	EXPECT_EQ( read.stats.skipped_missing_definitions, 91u );
}

TEST_F( ManifestTest, DirectoryWithoutAManifestIsNotCompacted )
{
	EXPECT_FALSE( isCompactedDirectory( m_dir ) );
}

TEST_F( ManifestTest, DirectoryWithAManifestIsCompacted )
{
	writeManifest( m_dir, sample() );
	EXPECT_TRUE( isCompactedDirectory( m_dir ) );
}

TEST_F( ManifestTest, UnparseableManifestIsNotCompacted )
{
	{
		std::ofstream file { m_dir / MANIFEST_FILENAME };
		file << "{ this is not json";
	}
	EXPECT_FALSE( isCompactedDirectory( m_dir ) );
}

TEST_F( ManifestTest, ManifestFromAFutureVersionIsNotCompacted )
{
	auto manifest = sample();
	manifest.format_version = CHUNK_FORMAT_VERSION + 1;
	writeManifest( m_dir, manifest );

	// Refusing an unknown version is what stops a newer flattener's output being misread.
	EXPECT_FALSE( isCompactedDirectory( m_dir ) );
}

TEST_F( ManifestTest, ReadingAMissingManifestThrows )
{
	EXPECT_THROW( ( void ) readManifest( m_dir ), std::runtime_error );
}

TEST_F( ManifestTest, RoundTripsWithNoChunks )
{
	CompactManifest manifest {};
	manifest.relations_file = "relations.idhanptr";
	writeManifest( m_dir, manifest );

	const auto read = readManifest( m_dir );
	EXPECT_TRUE( read.chunks.empty() );
	EXPECT_TRUE( isCompactedDirectory( m_dir ) );
}

} // namespace
```

- [ ] **Step 2: Run it and verify it fails**

```bash
cmake --build build/debug --target IDHANCoreTests -j$(nproc)
```

Expected: FAIL, `fatal error: ptr/flatten/Manifest.hpp: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `tools/HydrusImporter/ptr-core/include/ptr/flatten/Manifest.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "ptr/flatten/ChunkFormat.hpp"

namespace idhan::hydrus::ptr
{

inline constexpr const char* MANIFEST_FILENAME { "compact_manifest.json" };

//! One chunk as listed in the manifest.
struct ChunkEntry
{
	std::string file;
	std::uint64_t records { 0 };
	std::uint64_t mappings { 0 };
};

//! What a whole flatten run produced. Counters are 64-bit: events_scanned alone exceeds 3e9.
struct FlattenStats
{
	std::uint64_t events_scanned { 0 };
	std::uint64_t mappings_after_collapse { 0 };
	std::uint64_t events_collapsed { 0 }; //!< events_scanned minus operations emitted
	std::uint64_t terminal_deletes { 0 };
	std::uint64_t skipped_files { 0 }; //!< update files that failed to parse
	std::uint64_t skipped_missing_definitions { 0 };
};

//! The index of a compacted directory. Its presence is what marks the directory as compacted,
//! which is why the flattener writes it last: a cancelled run leaves no manifest and therefore
//! cannot be half-imported.
struct CompactManifest
{
	std::uint32_t format_version { CHUNK_FORMAT_VERSION };
	std::int32_t first_update_index { 0 };
	std::int32_t last_update_index { 0 };
	std::uint64_t max_records_per_chunk { 0 };
	std::vector< ChunkEntry > chunks;
	std::string relations_file;
	FlattenStats stats;
};

void writeManifest( const std::filesystem::path& dir, const CompactManifest& manifest );

//! \throws std::runtime_error if the manifest is absent or unparseable.
CompactManifest readManifest( const std::filesystem::path& dir );

//! \return true if \p dir holds a readable manifest of a supported format version.
bool isCompactedDirectory( const std::filesystem::path& dir );

} // namespace idhan::hydrus::ptr
```

- [ ] **Step 4: Write the implementation**

Create `tools/HydrusImporter/ptr-core/src/Manifest.cpp`:

```cpp
#include "ptr/flatten/Manifest.hpp"

#include <json/json.h>

#include <spdlog/spdlog.h>

#include <format>
#include <fstream>
#include <stdexcept>

namespace idhan::hydrus::ptr
{

namespace
{

Json::Value statsToJson( const FlattenStats& stats )
{
	Json::Value json { Json::objectValue };
	json[ "events_scanned" ] = static_cast< Json::UInt64 >( stats.events_scanned );
	json[ "mappings_after_collapse" ] = static_cast< Json::UInt64 >( stats.mappings_after_collapse );
	json[ "events_collapsed" ] = static_cast< Json::UInt64 >( stats.events_collapsed );
	json[ "terminal_deletes" ] = static_cast< Json::UInt64 >( stats.terminal_deletes );
	json[ "skipped_files" ] = static_cast< Json::UInt64 >( stats.skipped_files );
	json[ "skipped_missing_definitions" ] = static_cast< Json::UInt64 >( stats.skipped_missing_definitions );
	return json;
}

FlattenStats statsFromJson( const Json::Value& json )
{
	FlattenStats stats {};
	if ( !json.isObject() ) return stats;

	stats.events_scanned = json.get( "events_scanned", Json::UInt64( 0 ) ).asUInt64();
	stats.mappings_after_collapse = json.get( "mappings_after_collapse", Json::UInt64( 0 ) ).asUInt64();
	stats.events_collapsed = json.get( "events_collapsed", Json::UInt64( 0 ) ).asUInt64();
	stats.terminal_deletes = json.get( "terminal_deletes", Json::UInt64( 0 ) ).asUInt64();
	stats.skipped_files = json.get( "skipped_files", Json::UInt64( 0 ) ).asUInt64();
	stats.skipped_missing_definitions = json.get( "skipped_missing_definitions", Json::UInt64( 0 ) ).asUInt64();
	return stats;
}

} // namespace

void writeManifest( const std::filesystem::path& dir, const CompactManifest& manifest )
{
	Json::Value root { Json::objectValue };
	root[ "format_version" ] = manifest.format_version;
	root[ "first_update_index" ] = manifest.first_update_index;
	root[ "last_update_index" ] = manifest.last_update_index;
	root[ "max_records_per_chunk" ] = static_cast< Json::UInt64 >( manifest.max_records_per_chunk );
	root[ "relations_file" ] = manifest.relations_file;
	root[ "stats" ] = statsToJson( manifest.stats );

	// An empty result must serialise as [] rather than null, so the array is seeded explicitly.
	Json::Value chunks { Json::arrayValue };
	for ( const auto& chunk : manifest.chunks )
	{
		Json::Value entry { Json::objectValue };
		entry[ "file" ] = chunk.file;
		entry[ "records" ] = static_cast< Json::UInt64 >( chunk.records );
		entry[ "mappings" ] = static_cast< Json::UInt64 >( chunk.mappings );
		chunks.append( entry );
	}
	root[ "chunks" ] = chunks;

	std::filesystem::create_directories( dir );

	const auto path = dir / MANIFEST_FILENAME;
	std::ofstream file { path, std::ios::trunc };
	if ( !file ) throw std::runtime_error( std::format( "Failed to open manifest {} for writing", path.string() ) );

	Json::StreamWriterBuilder builder;
	builder[ "indentation" ] = "\t";
	file << Json::writeString( builder, root );

	if ( !file ) throw std::runtime_error( std::format( "Failed to write manifest {}", path.string() ) );
}

CompactManifest readManifest( const std::filesystem::path& dir )
{
	const auto path = dir / MANIFEST_FILENAME;

	std::ifstream file { path };
	if ( !file ) throw std::runtime_error( std::format( "No manifest at {}", path.string() ) );

	Json::Value root;
	Json::CharReaderBuilder builder;
	std::string errors;
	if ( !Json::parseFromStream( builder, file, &root, &errors ) )
		throw std::runtime_error( std::format( "Failed to parse manifest {}: {}", path.string(), errors ) );

	CompactManifest manifest {};
	manifest.format_version = root.get( "format_version", 0 ).asUInt();
	manifest.first_update_index = root.get( "first_update_index", 0 ).asInt();
	manifest.last_update_index = root.get( "last_update_index", 0 ).asInt();
	manifest.max_records_per_chunk = root.get( "max_records_per_chunk", Json::UInt64( 0 ) ).asUInt64();
	manifest.relations_file = root.get( "relations_file", "" ).asString();
	manifest.stats = statsFromJson( root[ "stats" ] );

	const auto& chunks = root[ "chunks" ];
	if ( chunks.isArray() )
	{
		for ( const auto& entry : chunks )
		{
			if ( !entry.isObject() ) continue;
			manifest.chunks.push_back(
				ChunkEntry { entry.get( "file", "" ).asString(),
			                 entry.get( "records", Json::UInt64( 0 ) ).asUInt64(),
			                 entry.get( "mappings", Json::UInt64( 0 ) ).asUInt64() } );
		}
	}

	return manifest;
}

bool isCompactedDirectory( const std::filesystem::path& dir )
{
	try
	{
		const auto manifest = readManifest( dir );
		if ( manifest.format_version != CHUNK_FORMAT_VERSION )
		{
			spdlog::warn(
				"Manifest at {} is format version {}, this build understands {}",
				dir.string(),
				manifest.format_version,
				CHUNK_FORMAT_VERSION );
			return false;
		}
		return true;
	}
	catch ( const std::exception& e )
	{
		spdlog::debug( "{} is not a compacted directory: {}", dir.string(), e.what() );
		return false;
	}
}

} // namespace idhan::hydrus::ptr
```

- [ ] **Step 5: Run the tests and verify they pass**

```bash
cmake --build build/debug --target IDHANCoreTests -j$(nproc)
./build/debug/bin/IDHANCoreTests --gtest_filter='ManifestTest.*'
```

Expected: 7 tests, all PASS.

- [ ] **Step 6: Run the whole PTR suite to check nothing regressed**

```bash
./build/debug/bin/IDHANCoreTests --gtest_filter='PTR*:BucketSpillTest.*:DefinitionStoreTest.*:ChunkFormatTest.*:ManifestTest.*'
```

Expected: all PASS.

- [ ] **Step 7: Commit**

```bash
git add tools/HydrusImporter/ptr-core/include/ptr/flatten/Manifest.hpp tools/HydrusImporter/ptr-core/src/Manifest.cpp tests/core/ptr/manifest.cpp
git commit -m "feat: add compacted directory manifest

Records the chunk list, source update range and collapse stats. Its presence
marks a directory as compacted, and an unknown format version is refused, so
a partial or newer-format output cannot be misread as importable.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

**Remaining tasks (8-13) continue in:** `docs/superpowers/plans/2026-07-30-ptr-flattener-part3.md`
