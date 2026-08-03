# PTR Flattener Implementation Plan — Part 3 (Tasks 8-10)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax for tracking.

Continues `docs/superpowers/plans/2026-07-30-ptr-flattener-part2.md`. The **Global Constraints** in part 1 apply to every task here.

---

### Task 8: Scan stage

Walks the corpus in update order, writing definitions to the flat store and mapping events to buckets. Relationship events accumulate in memory — at ~186,000 pairs across the whole corpus they do not justify a spill.

The Hydrus JSON shape this consumes, confirmed against real files: the root is `[serialisable_type, version, serialisable_info]`. For a definitions update the info is `[[0, [[hash_id, "hex"], ...]], [1, [[tag_id, "tag"], ...]]]`. For a content update it is `[[content_type, [[action, rows], ...]], ...]` where mappings rows are `[tag_id, [hash_id, ...]]` and relationship rows are `[a_id, b_id]`.

**Files:**
- Create: `tools/HydrusImporter/ptr-core/include/ptr/flatten/FlattenScan.hpp`
- Create: `tools/HydrusImporter/ptr-core/src/FlattenScan.cpp`
- Create: `tests/core/ptr/SyntheticCorpus.hpp`
- Test: `tests/core/ptr/flattenScan.cpp`

**Interfaces:**
- Consumes: `MappingEvent`, `MAX_UPDATE_INDEX` (Task 2); `BucketWriter`, `readBucket`, `bucketPath` (Task 4); `DefinitionWriter` (Task 5); `MetadataUpdate`, `parseUpdateFile` (Task 1).
- Produces: `struct RelationEvent { std::uint32_t a, b; std::uint16_t update_index; std::uint8_t op, pad; }`; `struct ScanCallbacks`; `struct ScanResult`; `ScanResult scanCorpus( const std::filesystem::path& ptr_dir, const MetadataUpdate& metadata, const std::filesystem::path& work_dir, const ScanCallbacks& callbacks )`.

- [ ] **Step 1: Write the synthetic corpus helper**

Create `tests/core/ptr/SyntheticCorpus.hpp`:

```cpp
//
// Created by kj16609 on 7/30/26.
//
// Builds real zlib-compressed .ptrupdate files so the scan stage is exercised through the same
// parser the production path uses, rather than against a mocked-out reader.

#pragma once

#include <zlib.h>

#include <json/json.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ptr/PTRConstants.hpp"
#include "ptr/PTRFileParser.hpp"

namespace idhan::hydrus::ptr::test
{

//! Compresses \p root and writes it to \p dir / (\p hash_hex + ".ptrupdate").
inline void writeUpdateFile( const std::filesystem::path& dir, const std::string& hash_hex, const Json::Value& root )
{
	Json::StreamWriterBuilder builder;
	builder[ "indentation" ] = "";
	const auto text = Json::writeString( builder, root );

	uLongf bound = ::compressBound( static_cast< uLong >( text.size() ) );
	std::vector< char > compressed( bound );

	const auto result = ::compress2(
		reinterpret_cast< Bytef* >( compressed.data() ),
		&bound,
		reinterpret_cast< const Bytef* >( text.data() ),
		static_cast< uLong >( text.size() ),
		Z_DEFAULT_COMPRESSION );

	if ( result != Z_OK ) throw std::runtime_error( "Failed to compress a synthetic update file" );

	compressed.resize( bound );

	std::filesystem::create_directories( dir );
	std::ofstream file { dir / ( hash_hex + ".ptrupdate" ), std::ios::binary | std::ios::trunc };
	file.write( compressed.data(), static_cast< std::streamsize >( compressed.size() ) );
	if ( !file ) throw std::runtime_error( "Failed to write a synthetic update file" );
}

//! Root wrapper: [serialisable_type, version, serialisable_info].
inline Json::Value makeRoot( const int serialisable_type, Json::Value info )
{
	Json::Value root { Json::arrayValue };
	root.append( serialisable_type );
	root.append( 1 );
	root.append( std::move( info ) );
	return root;
}

//! A definitions update carrying the given hash and tag definitions.
inline Json::Value makeDefinitions( const std::vector< std::pair< std::uint32_t, std::string > >& hashes,
                                    const std::vector< std::pair< std::uint32_t, std::string > >& tags )
{
	Json::Value info { Json::arrayValue };

	Json::Value hash_rows { Json::arrayValue };
	for ( const auto& [ id, hex ] : hashes )
	{
		Json::Value row { Json::arrayValue };
		row.append( static_cast< Json::UInt >( id ) );
		row.append( hex );
		hash_rows.append( row );
	}
	Json::Value hash_block { Json::arrayValue };
	hash_block.append( DEFINITIONS_TYPE_HASHES );
	hash_block.append( hash_rows );
	info.append( hash_block );

	Json::Value tag_rows { Json::arrayValue };
	for ( const auto& [ id, text ] : tags )
	{
		Json::Value row { Json::arrayValue };
		row.append( static_cast< Json::UInt >( id ) );
		row.append( text );
		tag_rows.append( row );
	}
	Json::Value tag_block { Json::arrayValue };
	tag_block.append( DEFINITIONS_TYPE_TAGS );
	tag_block.append( tag_rows );
	info.append( tag_block );

	return makeRoot( SERIALISABLE_TYPE_DEFINITIONS_UPDATE, info );
}

//! One (tag_id -> hash_ids) mapping row.
using MappingRow = std::pair< std::uint32_t, std::vector< std::uint32_t > >;

//! A content update. Any of the four lists may be empty.
inline Json::Value makeContent( const std::vector< MappingRow >& mappings_add,
                                const std::vector< MappingRow >& mappings_delete,
                                const std::vector< std::pair< std::uint32_t, std::uint32_t > >& siblings_add = {},
                                const std::vector< std::pair< std::uint32_t, std::uint32_t > >& parents_add = {} )
{
	const auto mappingRows = []( const std::vector< MappingRow >& rows )
	{
		Json::Value out { Json::arrayValue };
		for ( const auto& [ tag_id, hash_ids ] : rows )
		{
			Json::Value hashes { Json::arrayValue };
			for ( const auto hash_id : hash_ids ) hashes.append( static_cast< Json::UInt >( hash_id ) );

			Json::Value row { Json::arrayValue };
			row.append( static_cast< Json::UInt >( tag_id ) );
			row.append( hashes );
			out.append( row );
		}
		return out;
	};

	const auto pairRows = []( const std::vector< std::pair< std::uint32_t, std::uint32_t > >& rows )
	{
		Json::Value out { Json::arrayValue };
		for ( const auto& [ a, b ] : rows )
		{
			Json::Value row { Json::arrayValue };
			row.append( static_cast< Json::UInt >( a ) );
			row.append( static_cast< Json::UInt >( b ) );
			out.append( row );
		}
		return out;
	};

	const auto actionBlock = []( const int action, Json::Value rows )
	{
		Json::Value block { Json::arrayValue };
		block.append( action );
		block.append( std::move( rows ) );
		return block;
	};

	Json::Value info { Json::arrayValue };

	Json::Value mapping_actions { Json::arrayValue };
	if ( !mappings_add.empty() ) mapping_actions.append( actionBlock( CONTENT_UPDATE_ADD, mappingRows( mappings_add ) ) );
	if ( !mappings_delete.empty() )
		mapping_actions.append( actionBlock( CONTENT_UPDATE_DELETE, mappingRows( mappings_delete ) ) );
	if ( !mapping_actions.empty() )
	{
		Json::Value block { Json::arrayValue };
		block.append( CONTENT_TYPE_MAPPINGS );
		block.append( mapping_actions );
		info.append( block );
	}

	if ( !siblings_add.empty() )
	{
		Json::Value actions { Json::arrayValue };
		actions.append( actionBlock( CONTENT_UPDATE_ADD, pairRows( siblings_add ) ) );
		Json::Value block { Json::arrayValue };
		block.append( CONTENT_TYPE_TAG_SIBLINGS );
		block.append( actions );
		info.append( block );
	}

	if ( !parents_add.empty() )
	{
		Json::Value actions { Json::arrayValue };
		actions.append( actionBlock( CONTENT_UPDATE_ADD, pairRows( parents_add ) ) );
		Json::Value block { Json::arrayValue };
		block.append( CONTENT_TYPE_TAG_PARENTS );
		block.append( actions );
		info.append( block );
	}

	return makeRoot( SERIALISABLE_TYPE_CONTENT_UPDATE, info );
}

//! A 64-character hex string derived from \p seed, usable as both a file name and a hash value.
inline std::string fakeHashHex( const unsigned seed )
{
	std::string out;
	out.reserve( 64 );
	for ( int i = 0; i < 32; ++i )
	{
		constexpr char DIGITS[] = "0123456789abcdef";
		const auto byte = static_cast< unsigned >( ( seed + static_cast< unsigned >( i ) ) & 0xFFu );
		out.push_back( DIGITS[ ( byte >> 4 ) & 0xF ] );
		out.push_back( DIGITS[ byte & 0xF ] );
	}
	return out;
}

//! A MetadataUpdate listing \p files under a single update index.
inline MetadataUpdate makeMetadata( const std::vector< std::pair< int, std::vector< std::string > > >& updates )
{
	MetadataUpdate metadata {};
	for ( const auto& [ index, hashes ] : updates )
	{
		MetadataUpdateEntry entry {};
		entry.index = index;
		entry.hashes = hashes;
		entry.begin = 0;
		entry.end = 0;
		metadata.updates.push_back( entry );
	}
	return metadata;
}

} // namespace idhan::hydrus::ptr::test
```

- [ ] **Step 2: Write the failing test**

Create `tests/core/ptr/flattenScan.cpp`:

```cpp
//
// Created by kj16609 on 7/30/26.
//

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>

#include "SyntheticCorpus.hpp"
#include "ptr/flatten/BucketSpill.hpp"
#include "ptr/flatten/DefinitionStore.hpp"
#include "ptr/flatten/FlattenScan.hpp"

namespace
{

using namespace idhan::hydrus::ptr;
using namespace idhan::hydrus::ptr::test;

class FlattenScanTest : public ::testing::Test
{
  protected:

	void SetUp() override
	{
		m_root = std::filesystem::temp_directory_path() / "ptr-scan-test";
		std::filesystem::remove_all( m_root );
		m_corpus = m_root / "corpus";
		m_work = m_root / "work";
		std::filesystem::create_directories( m_corpus );
		std::filesystem::create_directories( m_work );
	}

	void TearDown() override { std::filesystem::remove_all( m_root ); }

	//! Every event the scan spilled, gathered across all buckets and sorted.
	std::vector< MappingEvent > allEvents() const
	{
		std::vector< MappingEvent > out;
		for ( std::size_t bucket = 0; bucket < BUCKET_COUNT; ++bucket )
		{
			const auto events = readBucket( bucketPath( m_work, bucket ) );
			out.insert( out.end(), events.begin(), events.end() );
		}
		std::ranges::sort( out, eventLess );
		return out;
	}

	std::filesystem::path m_root;
	std::filesystem::path m_corpus;
	std::filesystem::path m_work;
	ScanCallbacks m_callbacks {};
};

TEST_F( FlattenScanTest, WritesDefinitionsAndMappingEvents )
{
	const auto defs_hash = fakeHashHex( 1 );
	const auto content_hash = fakeHashHex( 2 );

	writeUpdateFile( m_corpus, defs_hash, makeDefinitions( { { 100, fakeHashHex( 50 ) } }, { { 7, "solo" } } ) );
	writeUpdateFile( m_corpus, content_hash, makeContent( { { 7, { 100 } } }, {} ) );

	const auto metadata = makeMetadata( { { 0, { defs_hash, content_hash } } } );
	const auto result = scanCorpus( m_corpus, metadata, m_work, m_callbacks );

	EXPECT_FALSE( result.cancelled );
	EXPECT_EQ( result.skipped_files, 0u );
	EXPECT_EQ( result.events_written, 1u );

	const auto events = allEvents();
	ASSERT_EQ( events.size(), 1u );
	EXPECT_EQ( events[ 0 ].hash_id, 100u );
	EXPECT_EQ( events[ 0 ].tag_id, 7u );
	EXPECT_EQ( events[ 0 ].update_index, 0u );
	EXPECT_EQ( events[ 0 ].op, static_cast< std::uint8_t >( EventOp::Add ) );

	const DefinitionReader reader { m_work };
	ASSERT_TRUE( reader.hash( 100 ).has_value() );
	const auto tag = reader.tag( 7 );
	ASSERT_TRUE( tag.has_value() );
	EXPECT_EQ( *tag, "solo" );
}

TEST_F( FlattenScanTest, RecordsAddsAndDeletesWithTheirUpdateIndex )
{
	const auto defs = fakeHashHex( 1 );
	const auto add_file = fakeHashHex( 2 );
	const auto del_file = fakeHashHex( 3 );

	writeUpdateFile( m_corpus, defs, makeDefinitions( { { 100, fakeHashHex( 50 ) } }, { { 7, "solo" } } ) );
	writeUpdateFile( m_corpus, add_file, makeContent( { { 7, { 100 } } }, {} ) );
	writeUpdateFile( m_corpus, del_file, makeContent( {}, { { 7, { 100 } } } ) );

	const auto metadata = makeMetadata( { { 0, { defs, add_file } }, { 5, { del_file } } } );
	const auto result = scanCorpus( m_corpus, metadata, m_work, m_callbacks );

	EXPECT_EQ( result.events_written, 2u );
	EXPECT_EQ( result.first_update_index, 0 );
	EXPECT_EQ( result.last_update_index, 5 );

	const auto events = allEvents();
	ASSERT_EQ( events.size(), 2u );
	EXPECT_EQ( events[ 0 ].op, static_cast< std::uint8_t >( EventOp::Add ) );
	EXPECT_EQ( events[ 0 ].update_index, 0u );
	EXPECT_EQ( events[ 1 ].op, static_cast< std::uint8_t >( EventOp::Delete ) );
	EXPECT_EQ( events[ 1 ].update_index, 5u );
}

TEST_F( FlattenScanTest, ProcessesUpdatesInAscendingIndexOrderRegardlessOfMetadataOrder )
{
	const auto defs = fakeHashHex( 1 );
	const auto later = fakeHashHex( 2 );
	const auto earlier = fakeHashHex( 3 );

	writeUpdateFile( m_corpus, defs, makeDefinitions( { { 1, fakeHashHex( 50 ) } }, { { 1, "a" } } ) );
	writeUpdateFile( m_corpus, later, makeContent( {}, { { 1, { 1 } } } ) );
	writeUpdateFile( m_corpus, earlier, makeContent( { { 1, { 1 } } }, {} ) );

	// Metadata deliberately lists index 9 before index 2.
	const auto metadata = makeMetadata( { { 9, { later } }, { 2, { defs, earlier } } } );
	const auto result = scanCorpus( m_corpus, metadata, m_work, m_callbacks );

	EXPECT_EQ( result.first_update_index, 2 );
	EXPECT_EQ( result.last_update_index, 9 );

	const auto events = allEvents();
	ASSERT_EQ( events.size(), 2u );
	EXPECT_EQ( events[ 0 ].update_index, 2u );
	EXPECT_EQ( events[ 1 ].update_index, 9u );
}

TEST_F( FlattenScanTest, MissingFileIsSkippedAndCounted )
{
	const auto defs = fakeHashHex( 1 );
	writeUpdateFile( m_corpus, defs, makeDefinitions( { { 1, fakeHashHex( 50 ) } }, { { 1, "a" } } ) );

	const auto metadata = makeMetadata( { { 0, { defs, fakeHashHex( 99 ) } } } );
	const auto result = scanCorpus( m_corpus, metadata, m_work, m_callbacks );

	EXPECT_EQ( result.skipped_files, 1u );
	EXPECT_EQ( result.events_written, 0u );
}

TEST_F( FlattenScanTest, UnparseableFileIsSkippedAndCounted )
{
	const auto defs = fakeHashHex( 1 );
	const auto broken = fakeHashHex( 2 );

	writeUpdateFile( m_corpus, defs, makeDefinitions( { { 1, fakeHashHex( 50 ) } }, { { 1, "a" } } ) );
	{
		std::ofstream file { m_corpus / ( broken + ".ptrupdate" ), std::ios::binary };
		file << "this is not zlib";
	}

	const auto metadata = makeMetadata( { { 0, { defs, broken } } } );
	const auto result = scanCorpus( m_corpus, metadata, m_work, m_callbacks );

	// One bad file must not abort the run -- the rest of the corpus still scans.
	EXPECT_EQ( result.skipped_files, 1u );
}

TEST_F( FlattenScanTest, MalformedHashDefinitionIsRejected )
{
	const auto defs = fakeHashHex( 1 );
	writeUpdateFile( m_corpus, defs, makeDefinitions( { { 1, "too-short" } }, { { 1, "a" } } ) );

	const auto metadata = makeMetadata( { { 0, { defs } } } );
	const auto result = scanCorpus( m_corpus, metadata, m_work, m_callbacks );

	EXPECT_EQ( result.rejected_hashes, 1u );

	const DefinitionReader reader { m_work };
	EXPECT_FALSE( reader.hash( 1 ).has_value() );
}

TEST_F( FlattenScanTest, CollectsRelationshipEventsInMemory )
{
	const auto defs = fakeHashHex( 1 );
	const auto content = fakeHashHex( 2 );

	writeUpdateFile( m_corpus, defs, makeDefinitions( {}, { { 1, "bad" }, { 2, "good" }, { 3, "parent" } } ) );
	writeUpdateFile( m_corpus, content, makeContent( {}, {}, { { 1, 2 } }, { { 1, 3 } } ) );

	const auto metadata = makeMetadata( { { 4, { defs, content } } } );
	const auto result = scanCorpus( m_corpus, metadata, m_work, m_callbacks );

	ASSERT_EQ( result.siblings.size(), 1u );
	EXPECT_EQ( result.siblings[ 0 ].a, 1u );
	EXPECT_EQ( result.siblings[ 0 ].b, 2u );
	EXPECT_EQ( result.siblings[ 0 ].update_index, 4u );

	ASSERT_EQ( result.parents.size(), 1u );
	EXPECT_EQ( result.parents[ 0 ].a, 1u );
	EXPECT_EQ( result.parents[ 0 ].b, 3u );
}

TEST_F( FlattenScanTest, StopsWhenCancelled )
{
	const auto defs = fakeHashHex( 1 );
	writeUpdateFile( m_corpus, defs, makeDefinitions( { { 1, fakeHashHex( 50 ) } }, { { 1, "a" } } ) );

	for ( unsigned i = 2; i < 12; ++i )
		writeUpdateFile( m_corpus, fakeHashHex( i ), makeContent( { { 1, { 1 } } }, {} ) );

	std::vector< std::string > hashes { defs };
	for ( unsigned i = 2; i < 12; ++i ) hashes.push_back( fakeHashHex( i ) );

	int seen { 0 };
	m_callbacks.cancelled = [ &seen ] { return seen++ >= 3; };

	const auto result = scanCorpus( m_corpus, makeMetadata( { { 0, hashes } } ), m_work, m_callbacks );

	EXPECT_TRUE( result.cancelled );
	EXPECT_LT( result.events_written, 10u );
}

TEST_F( FlattenScanTest, RejectsAnUpdateIndexBeyondTheSixteenBitRange )
{
	const auto defs = fakeHashHex( 1 );
	writeUpdateFile( m_corpus, defs, makeDefinitions( { { 1, fakeHashHex( 50 ) } }, { { 1, "a" } } ) );

	const auto metadata = makeMetadata( { { static_cast< int >( MAX_UPDATE_INDEX ) + 1, { defs } } } );

	// Silently truncating would corrupt every chain's ordering, so this must be loud.
	EXPECT_THROW( ( void ) scanCorpus( m_corpus, metadata, m_work, m_callbacks ), std::runtime_error );
}

} // namespace
```

Add `#include <fstream>` and `#include <stdexcept>` to the test's include block.

- [ ] **Step 3: Run it and verify it fails**

```bash
cmake --build build/debug --target IDHANCoreTests -j$(nproc)
```

Expected: FAIL, `fatal error: ptr/flatten/FlattenScan.hpp: No such file or directory`.

- [ ] **Step 4: Write the header**

Create `tools/HydrusImporter/ptr-core/include/ptr/flatten/FlattenScan.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string_view>
#include <vector>

#include "ptr/PTRFileParser.hpp"
#include "ptr/flatten/MappingEvent.hpp"

namespace idhan::hydrus::ptr
{

#pragma pack( push, 1 )
//! An add-or-delete of one tag relationship. For parents (a, b) is (child, parent); for siblings
//! it is (bad, good). Held in memory rather than spilled: the whole corpus has roughly 186,000.
struct RelationEvent
{
	std::uint32_t a;
	std::uint32_t b;
	std::uint16_t update_index;
	std::uint8_t op; //!< EventOp
	std::uint8_t pad;
};
#pragma pack( pop )

static_assert( sizeof( RelationEvent ) == 12 );

//! Host hooks. Both may be empty; scanCorpus checks before calling.
struct ScanCallbacks
{
	//! Polled once per update file. Return true to stop early.
	std::function< bool() > cancelled;
	//! (files done, files total, current status text)
	std::function< void( std::size_t, std::size_t, std::string_view ) > progress;
};

//! What the scan produced. Mapping events are on disk in \p work_dir's buckets; relationships and
//! the counters come back here.
struct ScanResult
{
	std::int32_t first_update_index { 0 };
	std::int32_t last_update_index { 0 };
	std::uint64_t events_written { 0 };
	std::uint64_t skipped_files { 0 }; //!< missing or unparseable
	std::uint64_t rejected_hashes { 0 };
	std::vector< RelationEvent > parents;
	std::vector< RelationEvent > siblings;
	bool cancelled { false };
};

//! Reads every update file listed in \p metadata, in ascending update index, writing definitions
//! into \p work_dir's definition store and mapping events into its buckets.
//!
//! A file that is missing or fails to parse is logged, counted in skipped_files, and stepped over:
//! one bad file must not abort a multi-hour run.
//!
//! \throws std::runtime_error if an update index exceeds MAX_UPDATE_INDEX. Truncating it would
//!         silently corrupt chain ordering, so this fails loudly instead.
ScanResult scanCorpus( const std::filesystem::path& ptr_dir,
                       const MetadataUpdate& metadata,
                       const std::filesystem::path& work_dir,
                       const ScanCallbacks& callbacks );

} // namespace idhan::hydrus::ptr
```

- [ ] **Step 5: Write the implementation**

Create `tools/HydrusImporter/ptr-core/src/FlattenScan.cpp`:

```cpp
#include "ptr/flatten/FlattenScan.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <format>
#include <stdexcept>
#include <variant>

#include "ptr/flatten/BucketSpill.hpp"
#include "ptr/flatten/DefinitionStore.hpp"

namespace idhan::hydrus::ptr
{

namespace
{

void appendRelations( std::vector< RelationEvent >& out,
                      const std::vector< std::pair< int, int > >& pairs,
                      const std::uint16_t update_index,
                      const EventOp op )
{
	for ( const auto& [ a, b ] : pairs )
	{
		out.push_back( RelationEvent { static_cast< std::uint32_t >( a ),
			                           static_cast< std::uint32_t >( b ),
			                           update_index,
			                           static_cast< std::uint8_t >( op ),
			                           0 } );
	}
}

void spillMappings( BucketWriter& buckets,
                    const std::vector< ContentUpdateMapping >& mappings,
                    const std::uint16_t update_index,
                    const EventOp op )
{
	for ( const auto& mapping : mappings )
	{
		for ( const auto hash_id : mapping.hash_ids )
		{
			buckets.write( MappingEvent { static_cast< std::uint32_t >( hash_id ),
				                          static_cast< std::uint32_t >( mapping.tag_id ),
				                          update_index,
				                          static_cast< std::uint8_t >( op ),
				                          0 } );
		}
	}
}

} // namespace

ScanResult scanCorpus( const std::filesystem::path& ptr_dir,
                       const MetadataUpdate& metadata,
                       const std::filesystem::path& work_dir,
                       const ScanCallbacks& callbacks )
{
	ScanResult result {};

	// Definitions must be written before the content that references them, so the corpus is walked
	// in ascending update index no matter what order the metadata happens to list.
	auto updates = metadata.updates;
	std::ranges::sort( updates, []( const auto& a, const auto& b ) { return a.index < b.index; } );

	if ( updates.empty() )
	{
		spdlog::warn( "Scan asked to process an empty metadata list" );
		return result;
	}

	for ( const auto& update : updates )
	{
		if ( update.index < 0 || update.index > static_cast< int >( MAX_UPDATE_INDEX ) )
			throw std::runtime_error( std::format(
				"Update index {} is outside the range MappingEvent can represent (0..{})",
				update.index,
				MAX_UPDATE_INDEX ) );
	}

	result.first_update_index = updates.front().index;
	result.last_update_index = updates.back().index;

	std::size_t total_files { 0 };
	for ( const auto& update : updates ) total_files += update.hashes.size();

	std::filesystem::create_directories( work_dir );

	DefinitionWriter definitions { work_dir };
	BucketWriter buckets { work_dir };

	std::size_t done { 0 };

	for ( const auto& update : updates )
	{
		const auto update_index = static_cast< std::uint16_t >( update.index );

		for ( const auto& hash_hex : update.hashes )
		{
			if ( callbacks.cancelled && callbacks.cancelled() )
			{
				result.cancelled = true;
				buckets.flush();
				result.rejected_hashes = definitions.rejectedHashes();
				return result;
			}

			++done;

			const auto path = ptr_dir / ( hash_hex + ".ptrupdate" );

			if ( callbacks.progress )
				callbacks.progress( done, total_files, std::format( "Scanning update {}", update.index ) );

			if ( !std::filesystem::exists( path ) )
			{
				spdlog::warn( "Update file missing, skipping: {}", path.string() );
				++result.skipped_files;
				continue;
			}

			try
			{
				auto parsed = parseUpdateFile( path );

				if ( const auto* const defs = std::get_if< DefinitionsUpdate >( &parsed ) )
				{
					for ( const auto& [ hash_id, hex ] : defs->hash_ids_to_hashes )
						definitions.writeHash( static_cast< std::uint32_t >( hash_id ), hex );

					for ( const auto& [ tag_id, tag ] : defs->tag_ids_to_tags )
						definitions.writeTag( static_cast< std::uint32_t >( tag_id ), tag );
				}
				else if ( const auto* const content = std::get_if< ContentUpdate >( &parsed ) )
				{
					const auto before = buckets.written();

					spillMappings( buckets, content->mappings_add, update_index, EventOp::Add );
					spillMappings( buckets, content->mappings_delete, update_index, EventOp::Delete );

					result.events_written += buckets.written() - before;

					appendRelations( result.parents, content->tag_parents_add, update_index, EventOp::Add );
					appendRelations( result.parents, content->tag_parents_delete, update_index, EventOp::Delete );
					appendRelations( result.siblings, content->tag_siblings_add, update_index, EventOp::Add );
					appendRelations( result.siblings, content->tag_siblings_delete, update_index, EventOp::Delete );
				}
			}
			catch ( const std::exception& e )
			{
				// One unreadable file must not end a multi-hour scan.
				spdlog::error( "Failed to scan {}: {}", hash_hex, e.what() );
				++result.skipped_files;
			}
		}
	}

	buckets.flush();
	result.rejected_hashes = definitions.rejectedHashes();

	spdlog::info(
		"Scan complete: {} events, {} files skipped, {} hashes rejected, {} parent and {} sibling events",
		result.events_written,
		result.skipped_files,
		result.rejected_hashes,
		result.parents.size(),
		result.siblings.size() );

	return result;
}

} // namespace idhan::hydrus::ptr
```

- [ ] **Step 6: Run the tests and verify they pass**

```bash
cmake --build build/debug --target IDHANCoreTests -j$(nproc)
./build/debug/bin/IDHANCoreTests --gtest_filter='FlattenScanTest.*'
```

Expected: 9 tests, all PASS.

- [ ] **Step 7: Commit**

```bash
git add tools/HydrusImporter/ptr-core/include/ptr/flatten/FlattenScan.hpp tools/HydrusImporter/ptr-core/src/FlattenScan.cpp tests/core/ptr/flattenScan.cpp tests/core/ptr/SyntheticCorpus.hpp
git commit -m "feat: add PTR corpus scan stage

Walks update files in ascending index, writing definitions to the flat store
and mapping events to hash-partitioned buckets. Relationship events stay in
memory; the whole corpus holds roughly 186,000 of them.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 9: Collapse stage

Reads each bucket, sorts it, collapses every chain, and emits record-major chunks. The subtlety worth getting right: a bucket holds only about 47,600 records, so a worker must keep its chunk open **across** bucket boundaries or `MAX_RECORDS_PER_CHUNK` can never be reached. Safe because every event for a record lives in exactly one bucket, so a record is never split.

**Files:**
- Create: `tools/HydrusImporter/ptr-core/include/ptr/flatten/FlattenCollapse.hpp`
- Create: `tools/HydrusImporter/ptr-core/src/FlattenCollapse.cpp`
- Test: `tests/core/ptr/flattenCollapse.cpp`

**Interfaces:**
- Consumes: `collapseChain` (Task 3); `readBucket`, `bucketPath` (Task 4); `DefinitionReader` (Task 5); `ChunkWriter`, `ChunkStats` (Task 6); `ChunkEntry`, `FlattenStats` (Task 7).
- Produces: `constexpr std::size_t MAX_RECORDS_PER_CHUNK`; `struct CollapseCallbacks`; `struct CollapseResult { std::vector< ChunkEntry > chunks; FlattenStats stats; bool cancelled; }`; `CollapseResult collapseBuckets( const std::filesystem::path& work_dir, const std::filesystem::path& out_dir, const DefinitionReader& definitions, std::size_t max_records_per_chunk, const CollapseCallbacks& callbacks )`.

- [ ] **Step 1: Write the failing test**

Create `tests/core/ptr/flattenCollapse.cpp`:

```cpp
//
// Created by kj16609 on 7/30/26.
//

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <map>

#include "SyntheticCorpus.hpp"
#include "ptr/flatten/BucketSpill.hpp"
#include "ptr/flatten/ChunkFormat.hpp"
#include "ptr/flatten/DefinitionStore.hpp"
#include "ptr/flatten/FlattenCollapse.hpp"

namespace
{

using namespace idhan::hydrus::ptr;
using namespace idhan::hydrus::ptr::test;

class FlattenCollapseTest : public ::testing::Test
{
  protected:

	void SetUp() override
	{
		m_root = std::filesystem::temp_directory_path() / "ptr-collapse-test";
		std::filesystem::remove_all( m_root );
		m_work = m_root / "work";
		m_out = m_root / "out";
		std::filesystem::create_directories( m_work );
		std::filesystem::create_directories( m_out );
	}

	void TearDown() override { std::filesystem::remove_all( m_root ); }

	//! Writes definitions for the given ids so the collapse stage can resolve them.
	void defineTags( const std::vector< std::pair< std::uint32_t, std::string > >& tags )
	{
		DefinitionWriter writer { m_work };
		for ( const auto& [ id, text ] : tags ) writer.writeTag( id, text );
	}

	void spill( const std::vector< MappingEvent >& events )
	{
		BucketWriter writer { m_work };
		for ( const auto& event : events ) writer.write( event );
	}

	//! Every record across every emitted chunk, keyed by its hash byte, with tag text resolved.
	struct FlatRecord
	{
		std::vector< std::string > adds;
		std::vector< std::string > dels;
	};

	std::map< unsigned, FlatRecord > readAll( const CollapseResult& result ) const
	{
		std::map< unsigned, FlatRecord > out;
		for ( const auto& entry : result.chunks )
		{
			const auto chunk = readChunk( m_out / entry.file );
			for ( const auto& record : chunk.records )
			{
				FlatRecord flat {};
				for ( const auto index : record.add_indices ) flat.adds.push_back( chunk.strings[ index ].tag );
				for ( const auto index : record.del_indices ) flat.dels.push_back( chunk.strings[ index ].tag );
				std::ranges::sort( flat.adds );
				std::ranges::sort( flat.dels );
				out.emplace( static_cast< unsigned >( record.sha256[ 0 ] ), std::move( flat ) );
			}
		}
		return out;
	}

	MappingEvent ev( const std::uint32_t hash_id,
	                 const std::uint32_t tag_id,
	                 const std::uint16_t index,
	                 const EventOp op ) const
	{
		return MappingEvent { hash_id, tag_id, index, static_cast< std::uint8_t >( op ), 0 };
	}

	//! Writes hash definitions whose first byte equals the hash id, so readAll can key on it.
	void defineHashes( const std::vector< std::uint32_t >& hash_ids )
	{
		DefinitionWriter writer { m_work };
		for ( const auto id : hash_ids ) writer.writeHash( id, fakeHashHex( id ) );
	}

	std::filesystem::path m_root;
	std::filesystem::path m_work;
	std::filesystem::path m_out;
	CollapseCallbacks m_callbacks {};
};

TEST_F( FlattenCollapseTest, EmitsASurvivingAdd )
{
	defineHashes( { 1 } );
	defineTags( { { 7, "solo" } } );
	spill( { ev( 1, 7, 0, EventOp::Add ) } );

	const DefinitionReader definitions { m_work };
	const auto result = collapseBuckets( m_work, m_out, definitions, MAX_RECORDS_PER_CHUNK, m_callbacks );

	EXPECT_FALSE( result.cancelled );
	EXPECT_EQ( result.stats.mappings_after_collapse, 1u );

	const auto records = readAll( result );
	ASSERT_EQ( records.size(), 1u );
	EXPECT_EQ( records.begin()->second.adds, std::vector< std::string > { "solo" } );
	EXPECT_TRUE( records.begin()->second.dels.empty() );
}

TEST_F( FlattenCollapseTest, CollapsesAddDeleteAddToASingleAdd )
{
	defineHashes( { 1 } );
	defineTags( { { 7, "solo" } } );
	spill( { ev( 1, 7, 0, EventOp::Add ), ev( 1, 7, 1, EventOp::Delete ), ev( 1, 7, 2, EventOp::Add ) } );

	const DefinitionReader definitions { m_work };
	const auto result = collapseBuckets( m_work, m_out, definitions, MAX_RECORDS_PER_CHUNK, m_callbacks );

	EXPECT_EQ( result.stats.events_scanned, 3u );
	EXPECT_EQ( result.stats.mappings_after_collapse, 1u );
	EXPECT_EQ( result.stats.events_collapsed, 2u );

	const auto records = readAll( result );
	ASSERT_EQ( records.size(), 1u );
	EXPECT_EQ( records.begin()->second.adds, std::vector< std::string > { "solo" } );
	EXPECT_TRUE( records.begin()->second.dels.empty() );
}

TEST_F( FlattenCollapseTest, CollapsesAddDeleteAddDeleteToASingleDelete )
{
	defineHashes( { 1 } );
	defineTags( { { 7, "solo" } } );
	spill( { ev( 1, 7, 0, EventOp::Add ),
	         ev( 1, 7, 1, EventOp::Delete ),
	         ev( 1, 7, 2, EventOp::Add ),
	         ev( 1, 7, 3, EventOp::Delete ) } );

	const DefinitionReader definitions { m_work };
	const auto result = collapseBuckets( m_work, m_out, definitions, MAX_RECORDS_PER_CHUNK, m_callbacks );

	EXPECT_EQ( result.stats.terminal_deletes, 1u );

	const auto records = readAll( result );
	ASSERT_EQ( records.size(), 1u );
	EXPECT_TRUE( records.begin()->second.adds.empty() );
	EXPECT_EQ( records.begin()->second.dels, std::vector< std::string > { "solo" } );
}

TEST_F( FlattenCollapseTest, GroupsEveryTagForOneRecordIntoOneEntry )
{
	// The whole point of record-major output: a record touched across many updates appears once.
	defineHashes( { 1 } );
	defineTags( { { 1, "a" }, { 2, "b" }, { 3, "c" } } );
	spill( { ev( 1, 1, 0, EventOp::Add ), ev( 1, 2, 5, EventOp::Add ), ev( 1, 3, 9, EventOp::Add ) } );

	const DefinitionReader definitions { m_work };
	const auto result = collapseBuckets( m_work, m_out, definitions, MAX_RECORDS_PER_CHUNK, m_callbacks );

	const auto records = readAll( result );
	ASSERT_EQ( records.size(), 1u );
	EXPECT_EQ( records.begin()->second.adds, ( std::vector< std::string > { "a", "b", "c" } ) );
}

TEST_F( FlattenCollapseTest, SeparatesAddsAndDeletesOnOneRecord )
{
	defineHashes( { 1 } );
	defineTags( { { 1, "kept" }, { 2, "removed" } } );
	spill( { ev( 1, 1, 0, EventOp::Add ), ev( 1, 2, 0, EventOp::Add ), ev( 1, 2, 3, EventOp::Delete ) } );

	const DefinitionReader definitions { m_work };
	const auto result = collapseBuckets( m_work, m_out, definitions, MAX_RECORDS_PER_CHUNK, m_callbacks );

	const auto records = readAll( result );
	ASSERT_EQ( records.size(), 1u );
	EXPECT_EQ( records.begin()->second.adds, std::vector< std::string > { "kept" } );
	EXPECT_EQ( records.begin()->second.dels, std::vector< std::string > { "removed" } );
}

TEST_F( FlattenCollapseTest, ChunkCapSplitsOutputAndSpansBuckets )
{
	// 40 records spread over many buckets with a cap of 8 must produce several chunks. If a chunk
	// were confined to a single bucket the cap would never be reached and this would emit one
	// chunk per occupied bucket instead.
	constexpr std::uint32_t RECORDS { 40 };

	std::vector< std::uint32_t > hash_ids;
	std::vector< MappingEvent > events;
	for ( std::uint32_t i = 1; i <= RECORDS; ++i )
	{
		hash_ids.push_back( i );
		events.push_back( ev( i, 1, 0, EventOp::Add ) );
	}

	defineHashes( hash_ids );
	defineTags( { { 1, "a" } } );
	spill( events );

	const DefinitionReader definitions { m_work };
	const auto result = collapseBuckets( m_work, m_out, definitions, 8, m_callbacks );

	std::uint64_t total { 0 };
	for ( const auto& entry : result.chunks )
	{
		EXPECT_LE( entry.records, 8u );
		total += entry.records;
	}
	EXPECT_EQ( total, RECORDS );
	EXPECT_GE( result.chunks.size(), 5u );
	EXPECT_LE( result.chunks.size(), 6u );
}

TEST_F( FlattenCollapseTest, TagWithNoDefinitionIsDroppedAndCounted )
{
	defineHashes( { 1 } );
	defineTags( { { 1, "known" } } );
	spill( { ev( 1, 1, 0, EventOp::Add ), ev( 1, 999, 0, EventOp::Add ) } );

	const DefinitionReader definitions { m_work };
	const auto result = collapseBuckets( m_work, m_out, definitions, MAX_RECORDS_PER_CHUNK, m_callbacks );

	EXPECT_EQ( result.stats.skipped_missing_definitions, 1u );

	const auto records = readAll( result );
	ASSERT_EQ( records.size(), 1u );
	EXPECT_EQ( records.begin()->second.adds, std::vector< std::string > { "known" } );
}

TEST_F( FlattenCollapseTest, RecordWithNoHashDefinitionIsDropped )
{
	// Without a hash there is nothing to address the record by, so it cannot be emitted.
	defineTags( { { 1, "a" } } );
	spill( { ev( 1, 1, 0, EventOp::Add ) } );

	const DefinitionReader definitions { m_work };
	const auto result = collapseBuckets( m_work, m_out, definitions, MAX_RECORDS_PER_CHUNK, m_callbacks );

	EXPECT_TRUE( readAll( result ).empty() );
}

TEST_F( FlattenCollapseTest, EmptyWorkDirectoryProducesNoChunks )
{
	const DefinitionReader definitions { m_work };
	const auto result = collapseBuckets( m_work, m_out, definitions, MAX_RECORDS_PER_CHUNK, m_callbacks );

	EXPECT_TRUE( result.chunks.empty() );
	EXPECT_EQ( result.stats.events_scanned, 0u );
}

TEST_F( FlattenCollapseTest, StopsWhenCancelled )
{
	std::vector< std::uint32_t > hash_ids;
	std::vector< MappingEvent > events;
	for ( std::uint32_t i = 1; i <= 200; ++i )
	{
		hash_ids.push_back( i );
		events.push_back( ev( i, 1, 0, EventOp::Add ) );
	}
	defineHashes( hash_ids );
	defineTags( { { 1, "a" } } );
	spill( events );

	m_callbacks.cancelled = [] { return true; };

	const DefinitionReader definitions { m_work };
	const auto result = collapseBuckets( m_work, m_out, definitions, 8, m_callbacks );

	EXPECT_TRUE( result.cancelled );
}

} // namespace
```

- [ ] **Step 2: Run it and verify it fails**

```bash
cmake --build build/debug --target IDHANCoreTests -j$(nproc)
```

Expected: FAIL, `fatal error: ptr/flatten/FlattenCollapse.hpp: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `tools/HydrusImporter/ptr-core/include/ptr/flatten/FlattenCollapse.hpp`:

```cpp
#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string_view>
#include <vector>

#include "ptr/flatten/DefinitionStore.hpp"
#include "ptr/flatten/Manifest.hpp"

namespace idhan::hydrus::ptr
{

//! Records per output chunk. Compile-time by design, alongside PTRImportWorker::BATCH_SIZE.
//!
//! This bounds the importer too: it holds one chunk's records and their RecordIDs at a time.
//! At the default the full corpus produces roughly 975 chunks in place of 26,324 update files.
inline constexpr std::size_t MAX_RECORDS_PER_CHUNK { 200'000 };

//! Host hooks. Both may be empty; collapseBuckets checks before calling.
struct CollapseCallbacks
{
	//! Polled once per bucket. Return true to stop early.
	std::function< bool() > cancelled;
	//! (buckets done, buckets total, current status text)
	std::function< void( std::size_t, std::size_t, std::string_view ) > progress;
};

//! What the collapse produced.
struct CollapseResult
{
	std::vector< ChunkEntry > chunks;
	FlattenStats stats;
	bool cancelled { false };
};

//! Collapses every bucket in \p work_dir into record-major chunks in \p out_dir.
//!
//! Each bucket is read whole, sorted, and scanned once: sorting makes every (hash_id, tag_id)
//! chain contiguous and chronological, so collapsing is a local decision over a span. Because a
//! bucket holds every event for each of its records, a record's output is final when its span ends.
//!
//! The chunk stays open across bucket boundaries. A bucket holds far fewer records than the cap,
//! so confining a chunk to one bucket would make \p max_records_per_chunk unreachable. This is
//! safe because every event for a record lives in exactly one bucket.
//!
//! Records whose hash has no definition are dropped: there is no way to address them. Tags with no
//! definition are dropped from their record and counted.
CollapseResult collapseBuckets( const std::filesystem::path& work_dir,
                                const std::filesystem::path& out_dir,
                                const DefinitionReader& definitions,
                                std::size_t max_records_per_chunk,
                                const CollapseCallbacks& callbacks );

} // namespace idhan::hydrus::ptr
```

- [ ] **Step 4: Write the implementation**

Create `tools/HydrusImporter/ptr-core/src/FlattenCollapse.cpp`:

```cpp
#include "ptr/flatten/FlattenCollapse.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <format>
#include <memory>
#include <span>

#include "ptr/flatten/BucketSpill.hpp"
#include "ptr/flatten/ChunkFormat.hpp"
#include "ptr/flatten/CollapseChain.hpp"

namespace idhan::hydrus::ptr
{

namespace
{

//! Owns the chunk currently being filled and rolls over to a new one at the cap. Kept separate
//! from the bucket loop so a chunk can span buckets.
class ChunkSink
{
  public:

	ChunkSink( std::filesystem::path out_dir, const std::size_t cap, const TagLookup& lookup ) :
	  m_out_dir( std::move( out_dir ) ),
	  m_cap( cap == 0 ? 1 : cap ),
	  m_lookup( lookup )
	{}

	void add( std::array< std::byte, SHA256_BYTES > sha256,
	          std::vector< std::uint32_t > adds,
	          std::vector< std::uint32_t > dels )
	{
		if ( m_writer == nullptr )
		{
			m_current_name = std::format( "chunk-{:05}.idhanptr", m_chunk_index );
			m_writer = std::make_unique< ChunkWriter >( m_out_dir / m_current_name );
		}

		m_writer->addRecord( sha256, std::move( adds ), std::move( dels ) );

		if ( m_writer->recordCount() >= m_cap ) close();
	}

	//! Writes the open chunk, if any, and records it in the entry list.
	void close()
	{
		if ( m_writer == nullptr ) return;

		const auto records = m_writer->recordCount();
		const auto stats = m_writer->finish( m_lookup );

		m_entries.push_back( ChunkEntry { m_current_name, records, stats.mappings } );
		m_missing_definitions += stats.missing_definitions;

		m_writer.reset();
		++m_chunk_index;
	}

	std::vector< ChunkEntry >& entries() noexcept { return m_entries; }

	std::uint64_t missingDefinitions() const noexcept { return m_missing_definitions; }

  private:

	std::filesystem::path m_out_dir;
	std::size_t m_cap;
	const TagLookup& m_lookup;

	std::unique_ptr< ChunkWriter > m_writer;
	std::string m_current_name;
	std::size_t m_chunk_index { 0 };
	std::vector< ChunkEntry > m_entries;
	std::uint64_t m_missing_definitions { 0 };
};

} // namespace

CollapseResult collapseBuckets( const std::filesystem::path& work_dir,
                                const std::filesystem::path& out_dir,
                                const DefinitionReader& definitions,
                                const std::size_t max_records_per_chunk,
                                const CollapseCallbacks& callbacks )
{
	CollapseResult result {};

	std::filesystem::create_directories( out_dir );

	const TagLookup lookup = [ &definitions ]( const std::uint32_t tag_id ) { return definitions.tag( tag_id ); };

	ChunkSink sink { out_dir, max_records_per_chunk, lookup };

	for ( std::size_t bucket = 0; bucket < BUCKET_COUNT; ++bucket )
	{
		if ( callbacks.cancelled && callbacks.cancelled() )
		{
			result.cancelled = true;
			break;
		}

		if ( callbacks.progress )
			callbacks.progress( bucket + 1, BUCKET_COUNT, std::format( "Collapsing bucket {}", bucket ) );

		auto events = readBucket( bucketPath( work_dir, bucket ) );
		if ( events.empty() ) continue;

		result.stats.events_scanned += events.size();

		std::ranges::sort( events, eventLess );

		// Equal hash_id values are contiguous after the sort, and inside a record so are equal
		// tag_id values, so one linear walk yields whole records without any lookaside structure.
		std::size_t record_start { 0 };
		while ( record_start < events.size() )
		{
			const auto hash_id = events[ record_start ].hash_id;

			std::size_t record_end { record_start };
			while ( record_end < events.size() && events[ record_end ].hash_id == hash_id ) ++record_end;

			const auto sha = definitions.hash( hash_id );
			if ( !sha.has_value() )
			{
				spdlog::warn( "No hash definition for hash_id={}, dropping its {} events", hash_id, record_end - record_start );
				record_start = record_end;
				continue;
			}

			std::vector< std::uint32_t > adds;
			std::vector< std::uint32_t > dels;

			std::size_t chain_start { record_start };
			while ( chain_start < record_end )
			{
				const auto tag_id = events[ chain_start ].tag_id;

				std::size_t chain_end { chain_start };
				while ( chain_end < record_end && events[ chain_end ].tag_id == tag_id ) ++chain_end;

				const std::span< const MappingEvent > chain { events.data() + chain_start, chain_end - chain_start };
				if ( const auto collapsed = collapseChain( chain ); collapsed.has_value() )
				{
					if ( collapsed->op == EventOp::Add )
					{
						adds.push_back( tag_id );
					}
					else
					{
						dels.push_back( tag_id );
						++result.stats.terminal_deletes;
					}
					++result.stats.mappings_after_collapse;
				}

				chain_start = chain_end;
			}

			std::array< std::byte, SHA256_BYTES > sha_bytes {};
			std::ranges::copy( *sha, sha_bytes.begin() );

			sink.add( sha_bytes, std::move( adds ), std::move( dels ) );

			record_start = record_end;
		}
	}

	sink.close();

	result.chunks = std::move( sink.entries() );
	result.stats.skipped_missing_definitions = sink.missingDefinitions();
	result.stats.events_collapsed = result.stats.events_scanned - result.stats.mappings_after_collapse;

	spdlog::info(
		"Collapse complete: {} events scanned, {} mappings survived ({} collapsed away), {} chunks",
		result.stats.events_scanned,
		result.stats.mappings_after_collapse,
		result.stats.events_collapsed,
		result.chunks.size() );

	return result;
}

} // namespace idhan::hydrus::ptr
```

Note: this implementation is single-threaded. Parallelising across buckets is a follow-up — it requires one `ChunkSink` per worker and a merge of their entry lists, and correctness comes first. Record it in the task notes rather than attempting both at once.

- [ ] **Step 5: Run the tests and verify they pass**

```bash
cmake --build build/debug --target IDHANCoreTests -j$(nproc)
./build/debug/bin/IDHANCoreTests --gtest_filter='FlattenCollapseTest.*'
```

Expected: 10 tests, all PASS.

- [ ] **Step 6: Commit**

```bash
git add tools/HydrusImporter/ptr-core/include/ptr/flatten/FlattenCollapse.hpp tools/HydrusImporter/ptr-core/src/FlattenCollapse.cpp tests/core/ptr/flattenCollapse.cpp
git commit -m "feat: add PTR collapse stage producing record-major chunks

Sorts each bucket, collapses every add/delete chain over a contiguous span,
and emits records once with their final tag set. The chunk stays open across
bucket boundaries so the record cap is actually reachable.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 10: Relationship collapse and the relations file

Parents and siblings, collapsed by the same rule and written as one small file with its own string table.

**Files:**
- Create: `tools/HydrusImporter/ptr-core/include/ptr/flatten/RelationsFile.hpp`
- Create: `tools/HydrusImporter/ptr-core/src/RelationsFile.cpp`
- Test: `tests/core/ptr/relationsFile.cpp`

**Interfaces:**
- Consumes: `RelationEvent` (Task 8); `collapseChain`, `CollapsedOp` (Task 3); `TagLookup`, `RELATIONS_MAGIC`, `ChunkHeader` (Task 6).
- Produces: `struct CollapsedRelation { std::uint32_t a, b; EventOp op; }`; `std::vector< CollapsedRelation > collapseRelations( std::vector< RelationEvent > events )`; `struct RelationsFileStats`; `RelationsFileStats writeRelationsFile( const std::filesystem::path& path, const std::vector< CollapsedRelation >& parents, const std::vector< CollapsedRelation >& siblings, const TagLookup& lookup )`; `struct RelationsFile { std::vector< ChunkStringEntry > strings; std::vector< RelationRecord > parents, siblings; }`; `RelationsFile readRelationsFile( const std::filesystem::path& path )`; constant `RELATIONS_FILENAME`.

- [ ] **Step 1: Write the failing test**

Create `tests/core/ptr/relationsFile.cpp`:

```cpp
//
// Created by kj16609 on 7/30/26.
//

#include <gtest/gtest.h>

#include <filesystem>
#include <map>
#include <string>

#include "ptr/flatten/RelationsFile.hpp"

namespace
{

using namespace idhan::hydrus::ptr;

RelationEvent rel( const std::uint32_t a, const std::uint32_t b, const std::uint16_t index, const EventOp op )
{
	return RelationEvent { a, b, index, static_cast< std::uint8_t >( op ), 0 };
}

TagLookup lookupOver( const std::map< std::uint32_t, std::string >& table )
{
	return [ &table ]( const std::uint32_t tag_id ) -> std::optional< std::string_view >
	{
		const auto it = table.find( tag_id );
		if ( it == table.end() ) return std::nullopt;
		return std::string_view( it->second );
	};
}

TEST( PTRCollapseRelations, EmptyInputProducesNothing )
{
	EXPECT_TRUE( collapseRelations( {} ).empty() );
}

TEST( PTRCollapseRelations, LoneAddSurvives )
{
	const auto collapsed = collapseRelations( { rel( 1, 2, 0, EventOp::Add ) } );
	ASSERT_EQ( collapsed.size(), 1u );
	EXPECT_EQ( collapsed[ 0 ].a, 1u );
	EXPECT_EQ( collapsed[ 0 ].b, 2u );
	EXPECT_EQ( collapsed[ 0 ].op, EventOp::Add );
}

TEST( PTRCollapseRelations, AddDeleteAddCollapsesToOneAdd )
{
	const auto collapsed = collapseRelations(
		{ rel( 1, 2, 0, EventOp::Add ), rel( 1, 2, 1, EventOp::Delete ), rel( 1, 2, 2, EventOp::Add ) } );

	ASSERT_EQ( collapsed.size(), 1u );
	EXPECT_EQ( collapsed[ 0 ].op, EventOp::Add );
}

TEST( PTRCollapseRelations, AddDeleteCollapsesToOneDelete )
{
	const auto collapsed = collapseRelations( { rel( 1, 2, 0, EventOp::Add ), rel( 1, 2, 4, EventOp::Delete ) } );

	ASSERT_EQ( collapsed.size(), 1u );
	EXPECT_EQ( collapsed[ 0 ].op, EventOp::Delete );
}

TEST( PTRCollapseRelations, DistinctPairsAreIndependent )
{
	const auto collapsed = collapseRelations( { rel( 1, 2, 0, EventOp::Add ),
	                                            rel( 3, 4, 0, EventOp::Add ),
	                                            rel( 1, 2, 1, EventOp::Delete ) } );

	ASSERT_EQ( collapsed.size(), 2u );
	// Output is sorted by (a, b), so (1,2) comes first.
	EXPECT_EQ( collapsed[ 0 ].a, 1u );
	EXPECT_EQ( collapsed[ 0 ].op, EventOp::Delete );
	EXPECT_EQ( collapsed[ 1 ].a, 3u );
	EXPECT_EQ( collapsed[ 1 ].op, EventOp::Add );
}

TEST( PTRCollapseRelations, DirectionMatters )
{
	// (1,2) and (2,1) are different relationships and must not collapse together.
	const auto collapsed = collapseRelations( { rel( 1, 2, 0, EventOp::Add ), rel( 2, 1, 0, EventOp::Add ) } );
	EXPECT_EQ( collapsed.size(), 2u );
}

TEST( PTRCollapseRelations, UnorderedInputIsHandled )
{
	const auto collapsed = collapseRelations(
		{ rel( 1, 2, 5, EventOp::Delete ), rel( 1, 2, 1, EventOp::Add ), rel( 1, 2, 3, EventOp::Add ) } );

	ASSERT_EQ( collapsed.size(), 1u );
	EXPECT_EQ( collapsed[ 0 ].op, EventOp::Delete );
}

class RelationsFileTest : public ::testing::Test
{
  protected:

	void SetUp() override
	{
		m_dir = std::filesystem::temp_directory_path() / "ptr-relations-test";
		std::filesystem::remove_all( m_dir );
		std::filesystem::create_directories( m_dir );
		m_path = m_dir / RELATIONS_FILENAME;
	}

	void TearDown() override { std::filesystem::remove_all( m_dir ); }

	std::filesystem::path m_dir;
	std::filesystem::path m_path;
};

TEST_F( RelationsFileTest, RoundTripsParentsAndSiblings )
{
	const std::map< std::uint32_t, std::string > tags {
		{ 1, "bad" }, { 2, "good" }, { 3, "child" }, { 4, "parent" }
	};

	const std::vector< CollapsedRelation > siblings { { 1, 2, EventOp::Add } };
	const std::vector< CollapsedRelation > parents { { 3, 4, EventOp::Delete } };

	const auto stats = writeRelationsFile( m_path, parents, siblings, lookupOver( tags ) );
	EXPECT_EQ( stats.parents, 1u );
	EXPECT_EQ( stats.siblings, 1u );
	EXPECT_EQ( stats.missing_definitions, 0u );

	const auto file = readRelationsFile( m_path );

	ASSERT_EQ( file.siblings.size(), 1u );
	EXPECT_EQ( file.strings[ file.siblings[ 0 ].a_index ].tag, "bad" );
	EXPECT_EQ( file.strings[ file.siblings[ 0 ].b_index ].tag, "good" );
	EXPECT_EQ( file.siblings[ 0 ].op, EventOp::Add );

	ASSERT_EQ( file.parents.size(), 1u );
	EXPECT_EQ( file.strings[ file.parents[ 0 ].a_index ].tag, "child" );
	EXPECT_EQ( file.strings[ file.parents[ 0 ].b_index ].tag, "parent" );
	EXPECT_EQ( file.parents[ 0 ].op, EventOp::Delete );
}

TEST_F( RelationsFileTest, PairWithAnUndefinedTagIsDroppedAndCounted )
{
	const std::map< std::uint32_t, std::string > tags { { 1, "known" } };
	const std::vector< CollapsedRelation > siblings { { 1, 999, EventOp::Add } };

	const auto stats = writeRelationsFile( m_path, {}, siblings, lookupOver( tags ) );
	EXPECT_EQ( stats.siblings, 0u );
	EXPECT_EQ( stats.missing_definitions, 1u );

	EXPECT_TRUE( readRelationsFile( m_path ).siblings.empty() );
}

TEST_F( RelationsFileTest, RoundTripsAnEmptyFile )
{
	writeRelationsFile( m_path, {}, {}, lookupOver( {} ) );

	const auto file = readRelationsFile( m_path );
	EXPECT_TRUE( file.parents.empty() );
	EXPECT_TRUE( file.siblings.empty() );
	EXPECT_TRUE( file.strings.empty() );
}

TEST_F( RelationsFileTest, RejectsAChunkFileAsRelations )
{
	// The two formats share a header shape; only the magic distinguishes them.
	const std::map< std::uint32_t, std::string > tags { { 1, "a" } };
	{
		ChunkWriter writer { m_path };
		writer.addRecord( std::array< std::byte, SHA256_BYTES > {}, { 1 }, {} );
		writer.finish( lookupOver( tags ) );
	}

	EXPECT_THROW( ( void ) readRelationsFile( m_path ), std::runtime_error );
}

} // namespace
```

- [ ] **Step 2: Run it and verify it fails**

```bash
cmake --build build/debug --target IDHANCoreTests -j$(nproc)
```

Expected: FAIL, `fatal error: ptr/flatten/RelationsFile.hpp: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `tools/HydrusImporter/ptr-core/include/ptr/flatten/RelationsFile.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "ptr/flatten/ChunkFormat.hpp"
#include "ptr/flatten/FlattenScan.hpp"

namespace idhan::hydrus::ptr
{

inline constexpr const char* RELATIONS_FILENAME { "relations.idhanptr" };

//! One relationship after its chain has been reduced.
struct CollapsedRelation
{
	std::uint32_t a;
	std::uint32_t b;
	EventOp op;
};

//! Reduces every (a, b) pair's history by the same rule collapseChain applies to mappings.
//! \p events may be in any order; it is sorted internally.
//! \return Collapsed relations, sorted by (a, b).
std::vector< CollapsedRelation > collapseRelations( std::vector< RelationEvent > events );

//! Counts from writing a relations file.
struct RelationsFileStats
{
	std::uint64_t parents { 0 };
	std::uint64_t siblings { 0 };
	std::uint64_t missing_definitions { 0 }; //!< pairs dropped because a tag had no definition
};

//! One relationship as read back, with indices into RelationsFile::strings.
struct RelationRecord
{
	std::uint32_t a_index;
	std::uint32_t b_index;
	EventOp op;
};

//! The relations file, decompressed.
struct RelationsFile
{
	std::vector< ChunkStringEntry > strings;
	std::vector< RelationRecord > parents;
	std::vector< RelationRecord > siblings;
};

//! Writes both relationship kinds into one file with a shared string table. A pair either of
//! whose tags has no definition is dropped and counted; a half-resolved relationship is meaningless.
RelationsFileStats writeRelationsFile( const std::filesystem::path& path,
                                       const std::vector< CollapsedRelation >& parents,
                                       const std::vector< CollapsedRelation >& siblings,
                                       const TagLookup& lookup );

//! \throws std::runtime_error on bad magic, unknown version, or truncation.
RelationsFile readRelationsFile( const std::filesystem::path& path );

} // namespace idhan::hydrus::ptr
```

- [ ] **Step 4: Write the implementation**

Create `tools/HydrusImporter/ptr-core/src/RelationsFile.cpp`:

```cpp
#include "ptr/flatten/RelationsFile.hpp"

#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <fstream>
#include <stdexcept>
#include <unordered_map>

#include "ptr/flatten/CollapseChain.hpp"

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

template < typename T >
T takePod( const std::vector< std::byte >& body, std::size_t& offset, const char* const what )
{
	if ( offset + sizeof( T ) > body.size() )
		throw std::runtime_error( std::format( "Relations file truncated while reading {}", what ) );

	T value {};
	std::memcpy( &value, body.data() + offset, sizeof( T ) );
	offset += sizeof( T );
	return value;
}

bool relationLess( const RelationEvent& x, const RelationEvent& y )
{
	if ( x.a != y.a ) return x.a < y.a;
	if ( x.b != y.b ) return x.b < y.b;
	if ( x.update_index != y.update_index ) return x.update_index < y.update_index;
	return x.op < y.op;
}

} // namespace

std::vector< CollapsedRelation > collapseRelations( std::vector< RelationEvent > events )
{
	std::ranges::sort( events, relationLess );

	std::vector< CollapsedRelation > out;

	std::size_t start { 0 };
	while ( start < events.size() )
	{
		std::size_t end { start };
		while ( end < events.size() && events[ end ].a == events[ start ].a && events[ end ].b == events[ start ].b )
			++end;

		// Reuse the mapping rule verbatim by projecting each relation event onto a MappingEvent:
		// the chain logic is about op ordering, not about what the ids mean.
		std::vector< MappingEvent > chain;
		chain.reserve( end - start );
		for ( std::size_t i = start; i < end; ++i )
			chain.push_back( MappingEvent { events[ i ].a, events[ i ].b, events[ i ].update_index, events[ i ].op, 0 } );

		if ( const auto collapsed = collapseChain( chain ); collapsed.has_value() )
			out.push_back( CollapsedRelation { events[ start ].a, events[ start ].b, collapsed->op } );

		start = end;
	}

	return out;
}

RelationsFileStats writeRelationsFile( const std::filesystem::path& path,
                                       const std::vector< CollapsedRelation >& parents,
                                       const std::vector< CollapsedRelation >& siblings,
                                       const TagLookup& lookup )
{
	RelationsFileStats stats {};

	std::vector< std::uint32_t > distinct;
	for ( const auto* const list : { &parents, &siblings } )
	{
		for ( const auto& relation : *list )
		{
			distinct.push_back( relation.a );
			distinct.push_back( relation.b );
		}
	}
	std::ranges::sort( distinct );
	distinct.erase( std::ranges::unique( distinct ).begin(), distinct.end() );

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

	const auto encode = [ & ]( const std::vector< CollapsedRelation >& list, std::uint64_t& counter )
	{
		std::vector< RelationRecord > out;
		out.reserve( list.size() );
		for ( const auto& relation : list )
		{
			const auto a = id_to_index.find( relation.a );
			const auto b = id_to_index.find( relation.b );
			if ( a == id_to_index.end() || b == id_to_index.end() )
			{
				++stats.missing_definitions;
				continue;
			}
			out.push_back( RelationRecord { a->second, b->second, relation.op } );
			++counter;
		}
		return out;
	};

	const auto encoded_parents = encode( parents, stats.parents );
	const auto encoded_siblings = encode( siblings, stats.siblings );

	std::vector< std::byte > body;

	for ( const auto& entry : strings )
	{
		appendPod( body, entry.ptr_tag_id );
		appendPod( body, static_cast< std::uint32_t >( entry.tag.size() ) );
		const auto* const bytes = reinterpret_cast< const std::byte* >( entry.tag.data() );
		body.insert( body.end(), bytes, bytes + entry.tag.size() );
	}

	appendPod( body, static_cast< std::uint32_t >( encoded_parents.size() ) );
	appendPod( body, static_cast< std::uint32_t >( encoded_siblings.size() ) );

	for ( const auto* const list : { &encoded_parents, &encoded_siblings } )
	{
		for ( const auto& record : *list )
		{
			appendPod( body, record.a_index );
			appendPod( body, record.b_index );
			appendPod( body, static_cast< std::uint8_t >( record.op ) );
		}
	}

	uLongf bound = ::compressBound( static_cast< uLong >( body.size() ) );
	std::vector< std::byte > compressed( bound == 0 ? 1 : bound );
	if ( ::compress2(
			 reinterpret_cast< Bytef* >( compressed.data() ),
			 &bound,
			 reinterpret_cast< const Bytef* >( body.data() ),
			 static_cast< uLong >( body.size() ),
			 Z_DEFAULT_COMPRESSION )
		 != Z_OK )
		throw std::runtime_error( "zlib compress2 failed while writing the relations file" );
	compressed.resize( bound );

	ChunkHeader header {};
	header.magic = RELATIONS_MAGIC;
	header.version = CHUNK_FORMAT_VERSION;
	header.body_size = body.size();
	header.record_count = static_cast< std::uint32_t >( encoded_parents.size() + encoded_siblings.size() );
	header.string_count = static_cast< std::uint32_t >( strings.size() );

	std::ofstream file { path, std::ios::binary | std::ios::trunc };
	if ( !file ) throw std::runtime_error( std::format( "Failed to open {} for writing", path.string() ) );

	file.write( reinterpret_cast< const char* >( &header ), sizeof( header ) );
	file.write( reinterpret_cast< const char* >( compressed.data() ), static_cast< std::streamsize >( compressed.size() ) );
	if ( !file ) throw std::runtime_error( std::format( "Failed to write {}", path.string() ) );

	return stats;
}

RelationsFile readRelationsFile( const std::filesystem::path& path )
{
	std::ifstream file { path, std::ios::binary | std::ios::ate };
	if ( !file ) throw std::runtime_error( std::format( "Failed to open {}", path.string() ) );

	const auto file_size = static_cast< std::uint64_t >( file.tellg() );
	if ( file_size < sizeof( ChunkHeader ) )
		throw std::runtime_error( std::format( "{} is too small to hold a header", path.string() ) );

	file.seekg( 0 );

	ChunkHeader header {};
	file.read( reinterpret_cast< char* >( &header ), sizeof( header ) );

	if ( header.magic != RELATIONS_MAGIC )
		throw std::runtime_error( std::format( "{} is not a relations file", path.string() ) );
	if ( header.version != CHUNK_FORMAT_VERSION )
		throw std::runtime_error(
			std::format( "{} is format version {}, expected {}", path.string(), header.version, CHUNK_FORMAT_VERSION ) );

	std::vector< std::byte > compressed( file_size - sizeof( ChunkHeader ) );
	if ( !compressed.empty() )
		file.read( reinterpret_cast< char* >( compressed.data() ), static_cast< std::streamsize >( compressed.size() ) );

	std::vector< std::byte > body( header.body_size );
	if ( header.body_size > 0 )
	{
		uLongf produced = static_cast< uLongf >( header.body_size );
		if ( ::uncompress(
				 reinterpret_cast< Bytef* >( body.data() ),
				 &produced,
				 reinterpret_cast< const Bytef* >( compressed.data() ),
				 static_cast< uLong >( compressed.size() ) )
			 != Z_OK )
			throw std::runtime_error( "zlib uncompress failed while reading the relations file" );
	}

	RelationsFile out {};
	std::size_t offset { 0 };

	out.strings.reserve( header.string_count );
	for ( std::uint32_t i = 0; i < header.string_count; ++i )
	{
		ChunkStringEntry entry {};
		entry.ptr_tag_id = takePod< std::uint32_t >( body, offset, "a string table id" );
		const auto length = takePod< std::uint32_t >( body, offset, "a string table length" );

		if ( offset + length > body.size() ) throw std::runtime_error( "Relations file truncated inside the string table" );
		entry.tag.assign( reinterpret_cast< const char* >( body.data() + offset ), length );
		offset += length;

		out.strings.push_back( std::move( entry ) );
	}

	const auto parent_count = takePod< std::uint32_t >( body, offset, "the parent count" );
	const auto sibling_count = takePod< std::uint32_t >( body, offset, "the sibling count" );

	const auto takeRecords = [ & ]( const std::uint32_t count )
	{
		std::vector< RelationRecord > records;
		records.reserve( count );
		for ( std::uint32_t i = 0; i < count; ++i )
		{
			RelationRecord record {};
			record.a_index = takePod< std::uint32_t >( body, offset, "a relation a index" );
			record.b_index = takePod< std::uint32_t >( body, offset, "a relation b index" );
			record.op = static_cast< EventOp >( takePod< std::uint8_t >( body, offset, "a relation op" ) );
			records.push_back( record );
		}
		return records;
	};

	out.parents = takeRecords( parent_count );
	out.siblings = takeRecords( sibling_count );

	return out;
}

} // namespace idhan::hydrus::ptr
```

- [ ] **Step 5: Run the tests and verify they pass**

```bash
cmake --build build/debug --target IDHANCoreTests -j$(nproc)
./build/debug/bin/IDHANCoreTests --gtest_filter='PTRCollapseRelations.*:RelationsFileTest.*'
```

Expected: 11 tests, all PASS.

- [ ] **Step 6: Run the whole PTR suite**

```bash
./build/debug/bin/IDHANCoreTests --gtest_filter='PTR*:BucketSpillTest.*:DefinitionStoreTest.*:ChunkFormatTest.*:ManifestTest.*:FlattenScanTest.*:FlattenCollapseTest.*:RelationsFileTest.*'
```

Expected: all PASS.

- [ ] **Step 7: Commit**

```bash
git add tools/HydrusImporter/ptr-core/include/ptr/flatten/RelationsFile.hpp tools/HydrusImporter/ptr-core/src/RelationsFile.cpp tests/core/ptr/relationsFile.cpp
git commit -m "feat: collapse tag parents and siblings into a relations file

Applies the same chain reduction as mappings, projecting relation events onto
the mapping rule so the logic exists once. Both kinds share one string table
in a single small file.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

**Remaining tasks (11-13) continue in:** `docs/superpowers/plans/2026-07-30-ptr-flattener-part4.md`
