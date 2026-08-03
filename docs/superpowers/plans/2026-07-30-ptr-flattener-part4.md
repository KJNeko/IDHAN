# PTR Flattener Implementation Plan — Part 4 (Tasks 11-12)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax for tracking.

Continues `docs/superpowers/plans/2026-07-30-ptr-flattener-part3.md`. The **Global Constraints** in part 1 apply to every task here.

---

### Task 11: `runFlatten` orchestrator and the end-to-end test

The whole pipeline as one Qt-free function, so the property that matters — a synthetic corpus in, correct chunks out — is testable without an event loop, a database, or a server. The `QRunnable` in Task 12 becomes a thin wrapper over this.

Order is load-bearing: the manifest is written **last**, so a cancelled or crashed run leaves a directory that `isCompactedDirectory` rejects and therefore cannot be half-imported.

**Files:**
- Create: `tools/HydrusImporter/ptr-core/include/ptr/flatten/RunFlatten.hpp`
- Create: `tools/HydrusImporter/ptr-core/src/RunFlatten.cpp`
- Test: `tests/core/ptr/runFlatten.cpp`

**Interfaces:**
- Consumes: `scanCorpus`, `ScanCallbacks` (Task 8); `collapseBuckets`, `CollapseCallbacks`, `MAX_RECORDS_PER_CHUNK` (Task 9); `collapseRelations`, `writeRelationsFile`, `RELATIONS_FILENAME` (Task 10); `writeManifest`, `CompactManifest` (Task 7); `DefinitionReader` (Task 5); `MetadataUpdate`, `parseUpdateFile`, `parseMetadataCacheJson` (Task 1).
- Produces: `MetadataUpdate loadCorpusMetadata( const std::filesystem::path& dir )`; `struct FlattenCallbacks`; `struct FlattenOutcome`; `FlattenOutcome runFlatten( const std::filesystem::path& ptr_dir, const std::filesystem::path& out_dir, const FlattenCallbacks& callbacks, std::size_t max_records_per_chunk = MAX_RECORDS_PER_CHUNK )`; constants `WORK_SUBDIRECTORY`, `REQUIRED_FREE_BYTES`.

- [ ] **Step 1: Write the failing test**

Create `tests/core/ptr/runFlatten.cpp`:

```cpp
//
// Created by kj16609 on 7/30/26.
//

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#include "SyntheticCorpus.hpp"
#include "ptr/flatten/ChunkFormat.hpp"
#include "ptr/flatten/Manifest.hpp"
#include "ptr/flatten/RelationsFile.hpp"
#include "ptr/flatten/RunFlatten.hpp"

namespace
{

using namespace idhan::hydrus::ptr;
using namespace idhan::hydrus::ptr::test;

class RunFlattenTest : public ::testing::Test
{
  protected:

	void SetUp() override
	{
		m_root = std::filesystem::temp_directory_path() / "ptr-runflatten-test";
		std::filesystem::remove_all( m_root );
		m_corpus = m_root / "corpus";
		m_out = m_root / "out";
		std::filesystem::create_directories( m_corpus );
	}

	void TearDown() override { std::filesystem::remove_all( m_root ); }

	//! Writes a ptr_metadata.json in the shape parseMetadataCacheJson expects.
	void writeMetadataCache( const std::vector< std::pair< int, std::vector< std::string > > >& updates ) const
	{
		Json::Value root { Json::objectValue };
		Json::Value list { Json::arrayValue };
		for ( const auto& [ index, hashes ] : updates )
		{
			Json::Value entry { Json::objectValue };
			entry[ "index" ] = index;
			Json::Value hash_array { Json::arrayValue };
			for ( const auto& hash : hashes ) hash_array.append( hash );
			entry[ "hashes" ] = hash_array;
			entry[ "begin" ] = 0;
			entry[ "end" ] = 0;
			list.append( entry );
		}
		root[ "updates" ] = list;
		root[ "state" ] = "done";

		std::ofstream file { m_corpus / "ptr_metadata.json", std::ios::trunc };
		Json::StreamWriterBuilder builder;
		file << Json::writeString( builder, root );
	}

	//! Tag text applied to each record in the output, keyed by record hash hex.
	std::map< std::string, std::vector< std::string > > readAdds() const
	{
		std::map< std::string, std::vector< std::string > > out;
		const auto manifest = readManifest( m_out );

		for ( const auto& entry : manifest.chunks )
		{
			const auto chunk = readChunk( m_out / entry.file );
			for ( const auto& record : chunk.records )
			{
				std::string hex;
				for ( const auto byte : record.sha256 )
				{
					constexpr char DIGITS[] = "0123456789abcdef";
					const auto value = std::to_integer< unsigned >( byte );
					hex.push_back( DIGITS[ ( value >> 4 ) & 0xF ] );
					hex.push_back( DIGITS[ value & 0xF ] );
				}

				std::vector< std::string > tags;
				for ( const auto index : record.add_indices ) tags.push_back( chunk.strings[ index ].tag );
				std::ranges::sort( tags );
				out.emplace( hex, std::move( tags ) );
			}
		}
		return out;
	}

	std::filesystem::path m_root;
	std::filesystem::path m_corpus;
	std::filesystem::path m_out;
	FlattenCallbacks m_callbacks {};
};

TEST_F( RunFlattenTest, FlattensAChainAcrossUpdateFilesIntoOneAdd )
{
	// The motivating case end to end: tag 7 on record 100 is added, deleted, then re-added across
	// three separate update files. The compacted output must show exactly one add.
	const auto defs = fakeHashHex( 1 );
	const auto add1 = fakeHashHex( 2 );
	const auto del = fakeHashHex( 3 );
	const auto add2 = fakeHashHex( 4 );
	const auto record_hex = fakeHashHex( 100 );

	writeUpdateFile( m_corpus, defs, makeDefinitions( { { 100, record_hex } }, { { 7, "solo" } } ) );
	writeUpdateFile( m_corpus, add1, makeContent( { { 7, { 100 } } }, {} ) );
	writeUpdateFile( m_corpus, del, makeContent( {}, { { 7, { 100 } } } ) );
	writeUpdateFile( m_corpus, add2, makeContent( { { 7, { 100 } } }, {} ) );

	writeMetadataCache( { { 0, { defs, add1 } }, { 1, { del } }, { 2, { add2 } } } );

	const auto outcome = runFlatten( m_corpus, m_out, m_callbacks );

	ASSERT_TRUE( outcome.success );
	EXPECT_FALSE( outcome.cancelled );

	const auto adds = readAdds();
	ASSERT_EQ( adds.size(), 1u );
	EXPECT_EQ( adds.at( record_hex ), std::vector< std::string > { "solo" } );

	const auto manifest = readManifest( m_out );
	EXPECT_EQ( manifest.stats.events_scanned, 3u );
	EXPECT_EQ( manifest.stats.mappings_after_collapse, 1u );
	EXPECT_EQ( manifest.stats.events_collapsed, 2u );
	EXPECT_EQ( manifest.first_update_index, 0 );
	EXPECT_EQ( manifest.last_update_index, 2 );
	EXPECT_EQ( manifest.max_records_per_chunk, MAX_RECORDS_PER_CHUNK );
}

TEST_F( RunFlattenTest, ProducesACompactedDirectory )
{
	const auto defs = fakeHashHex( 1 );
	const auto content = fakeHashHex( 2 );

	writeUpdateFile( m_corpus, defs, makeDefinitions( { { 1, fakeHashHex( 100 ) } }, { { 1, "a" } } ) );
	writeUpdateFile( m_corpus, content, makeContent( { { 1, { 1 } } }, {} ) );
	writeMetadataCache( { { 0, { defs, content } } } );

	ASSERT_TRUE( runFlatten( m_corpus, m_out, m_callbacks ).success );

	EXPECT_TRUE( isCompactedDirectory( m_out ) );
	EXPECT_TRUE( std::filesystem::exists( m_out / RELATIONS_FILENAME ) );
}

TEST_F( RunFlattenTest, RemovesTheWorkDirectoryOnSuccess )
{
	const auto defs = fakeHashHex( 1 );
	writeUpdateFile( m_corpus, defs, makeDefinitions( { { 1, fakeHashHex( 100 ) } }, { { 1, "a" } } ) );
	writeMetadataCache( { { 0, { defs } } } );

	ASSERT_TRUE( runFlatten( m_corpus, m_out, m_callbacks ).success );

	// The buckets and definition store are scratch; leaving 45 GB behind would be a bug.
	EXPECT_FALSE( std::filesystem::exists( m_out / WORK_SUBDIRECTORY ) );
}

TEST_F( RunFlattenTest, TerminalDeleteSurvivesAsADelete )
{
	const auto defs = fakeHashHex( 1 );
	const auto add = fakeHashHex( 2 );
	const auto del = fakeHashHex( 3 );
	const auto record_hex = fakeHashHex( 100 );

	writeUpdateFile( m_corpus, defs, makeDefinitions( { { 100, record_hex } }, { { 7, "solo" } } ) );
	writeUpdateFile( m_corpus, add, makeContent( { { 7, { 100 } } }, {} ) );
	writeUpdateFile( m_corpus, del, makeContent( {}, { { 7, { 100 } } } ) );
	writeMetadataCache( { { 0, { defs, add } }, { 1, { del } } } );

	ASSERT_TRUE( runFlatten( m_corpus, m_out, m_callbacks ).success );

	const auto manifest = readManifest( m_out );
	EXPECT_EQ( manifest.stats.terminal_deletes, 1u );

	const auto chunk = readChunk( m_out / manifest.chunks.at( 0 ).file );
	ASSERT_EQ( chunk.records.size(), 1u );
	EXPECT_TRUE( chunk.records[ 0 ].add_indices.empty() );
	ASSERT_EQ( chunk.records[ 0 ].del_indices.size(), 1u );
	EXPECT_EQ( chunk.strings[ chunk.records[ 0 ].del_indices[ 0 ] ].tag, "solo" );
}

TEST_F( RunFlattenTest, WritesCollapsedRelations )
{
	const auto defs = fakeHashHex( 1 );
	const auto content = fakeHashHex( 2 );

	writeUpdateFile( m_corpus, defs, makeDefinitions( {}, { { 1, "bad" }, { 2, "good" }, { 3, "parent" } } ) );
	writeUpdateFile( m_corpus, content, makeContent( {}, {}, { { 1, 2 } }, { { 1, 3 } } ) );
	writeMetadataCache( { { 0, { defs, content } } } );

	ASSERT_TRUE( runFlatten( m_corpus, m_out, m_callbacks ).success );

	const auto relations = readRelationsFile( m_out / RELATIONS_FILENAME );
	ASSERT_EQ( relations.siblings.size(), 1u );
	EXPECT_EQ( relations.strings[ relations.siblings[ 0 ].a_index ].tag, "bad" );
	EXPECT_EQ( relations.strings[ relations.siblings[ 0 ].b_index ].tag, "good" );
	ASSERT_EQ( relations.parents.size(), 1u );
	EXPECT_EQ( relations.strings[ relations.parents[ 0 ].b_index ].tag, "parent" );
}

TEST_F( RunFlattenTest, GroupsManyUpdatesIntoFarFewerChunks )
{
	// The grouping win: 60 update files touching 5 records must compact to a single chunk with
	// each record appearing exactly once.
	const auto defs = fakeHashHex( 1 );

	std::vector< std::pair< std::uint32_t, std::string > > hash_defs;
	std::vector< std::pair< std::uint32_t, std::string > > tag_defs;
	for ( std::uint32_t r = 1; r <= 5; ++r ) hash_defs.emplace_back( r, fakeHashHex( 100 + r ) );
	for ( std::uint32_t t = 1; t <= 60; ++t ) tag_defs.emplace_back( t, "tag:" + std::to_string( t ) );

	writeUpdateFile( m_corpus, defs, makeDefinitions( hash_defs, tag_defs ) );

	std::vector< std::pair< int, std::vector< std::string > > > updates { { 0, { defs } } };
	for ( std::uint32_t t = 1; t <= 60; ++t )
	{
		const auto file = fakeHashHex( 200 + t );
		writeUpdateFile( m_corpus, file, makeContent( { { t, { 1, 2, 3, 4, 5 } } }, {} ) );
		updates.push_back( { static_cast< int >( t ), { file } } );
	}
	writeMetadataCache( updates );

	ASSERT_TRUE( runFlatten( m_corpus, m_out, m_callbacks ).success );

	const auto manifest = readManifest( m_out );
	EXPECT_EQ( manifest.chunks.size(), 1u );
	EXPECT_EQ( manifest.stats.events_scanned, 300u );
	EXPECT_EQ( manifest.stats.mappings_after_collapse, 300u );

	const auto adds = readAdds();
	EXPECT_EQ( adds.size(), 5u );
	for ( const auto& [ hex, tags ] : adds ) EXPECT_EQ( tags.size(), 60u );
}

TEST_F( RunFlattenTest, CancelledRunLeavesNoManifest )
{
	const auto defs = fakeHashHex( 1 );
	writeUpdateFile( m_corpus, defs, makeDefinitions( { { 1, fakeHashHex( 100 ) } }, { { 1, "a" } } ) );
	for ( unsigned i = 2; i < 20; ++i )
		writeUpdateFile( m_corpus, fakeHashHex( i ), makeContent( { { 1, { 1 } } }, {} ) );

	std::vector< std::string > hashes { defs };
	for ( unsigned i = 2; i < 20; ++i ) hashes.push_back( fakeHashHex( i ) );
	writeMetadataCache( { { 0, hashes } } );

	m_callbacks.cancelled = [] { return true; };

	const auto outcome = runFlatten( m_corpus, m_out, m_callbacks );

	EXPECT_TRUE( outcome.cancelled );
	EXPECT_FALSE( outcome.success );

	// This is the safety property: without a manifest the directory is not importable.
	EXPECT_FALSE( isCompactedDirectory( m_out ) );
}

TEST_F( RunFlattenTest, MissingMetadataFails )
{
	const auto outcome = runFlatten( m_corpus, m_out, m_callbacks );

	EXPECT_FALSE( outcome.success );
	EXPECT_FALSE( outcome.message.empty() );
	EXPECT_FALSE( isCompactedDirectory( m_out ) );
}

TEST_F( RunFlattenTest, ReportsProgress )
{
	const auto defs = fakeHashHex( 1 );
	const auto content = fakeHashHex( 2 );
	writeUpdateFile( m_corpus, defs, makeDefinitions( { { 1, fakeHashHex( 100 ) } }, { { 1, "a" } } ) );
	writeUpdateFile( m_corpus, content, makeContent( { { 1, { 1 } } }, {} ) );
	writeMetadataCache( { { 0, { defs, content } } } );

	int stages { 0 };
	m_callbacks.stage = [ &stages ]( const std::string_view ) { ++stages; };

	ASSERT_TRUE( runFlatten( m_corpus, m_out, m_callbacks ).success );
	EXPECT_GE( stages, 3 );
}

} // namespace
```

- [ ] **Step 2: Run it and verify it fails**

```bash
cmake --build build/debug --target IDHANCoreTests -j$(nproc)
```

Expected: FAIL, `fatal error: ptr/flatten/RunFlatten.hpp: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `tools/HydrusImporter/ptr-core/include/ptr/flatten/RunFlatten.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

#include "ptr/PTRFileParser.hpp"
#include "ptr/flatten/FlattenCollapse.hpp"
#include "ptr/flatten/Manifest.hpp"

namespace idhan::hydrus::ptr
{

//! Scratch subdirectory of the output directory. Holds the buckets and the definition store, and
//! is removed once the chunks are written -- chunks carry their own strings, so nothing in here
//! is needed at import time.
inline constexpr const char* WORK_SUBDIRECTORY { "work" };

//! Free space required before a flatten will start. The full corpus spills roughly 38 GB of
//! buckets plus 7.6 GB of definitions; 60 GB leaves room for the chunks written alongside.
inline constexpr std::uint64_t REQUIRED_FREE_BYTES { 60ULL * 1024 * 1024 * 1024 };

//! Host hooks. Every member may be empty; runFlatten checks before calling.
struct FlattenCallbacks
{
	//! Polled frequently. Return true to stop early.
	std::function< bool() > cancelled;
	//! Called once as each stage begins.
	std::function< void( std::string_view ) > stage;
	//! (done, total, status text) within the current stage.
	std::function< void( std::size_t, std::size_t, std::string_view ) > progress;
};

//! What a flatten run produced.
struct FlattenOutcome
{
	bool success { false };
	bool cancelled { false };
	std::string message;
	CompactManifest manifest;
};

//! Loads the corpus metadata, preferring the downloader's ptr_metadata.json and falling back to
//! the raw metadata.ptrupdate.
//! \throws std::runtime_error if neither is present or usable.
MetadataUpdate loadCorpusMetadata( const std::filesystem::path& dir );

//! Scans, collapses, writes relations, and finally writes the manifest.
//!
//! The manifest is written last on purpose: its presence is what marks a directory as compacted,
//! so a cancelled or crashed run leaves output that cannot be mistaken for importable.
//!
//! \param max_records_per_chunk Overridable for tests; production uses MAX_RECORDS_PER_CHUNK.
FlattenOutcome runFlatten( const std::filesystem::path& ptr_dir,
                           const std::filesystem::path& out_dir,
                           const FlattenCallbacks& callbacks,
                           std::size_t max_records_per_chunk = MAX_RECORDS_PER_CHUNK );

} // namespace idhan::hydrus::ptr
```

- [ ] **Step 4: Write the implementation**

Create `tools/HydrusImporter/ptr-core/src/RunFlatten.cpp`:

```cpp
#include "ptr/flatten/RunFlatten.hpp"

#include <json/json.h>

#include <spdlog/spdlog.h>

#include <format>
#include <fstream>
#include <stdexcept>
#include <variant>

#include "ptr/flatten/DefinitionStore.hpp"
#include "ptr/flatten/FlattenScan.hpp"
#include "ptr/flatten/RelationsFile.hpp"

namespace idhan::hydrus::ptr
{

namespace
{

void announce( const FlattenCallbacks& callbacks, const std::string_view text )
{
	spdlog::info( "Flatten: {}", text );
	if ( callbacks.stage ) callbacks.stage( text );
}

//! Removes the scratch directory, logging rather than throwing: failing to clean up must not
//! turn a successful flatten into a failed one.
void removeWorkDirectory( const std::filesystem::path& work_dir )
{
	std::error_code ec;
	std::filesystem::remove_all( work_dir, ec );
	if ( ec ) spdlog::warn( "Failed to remove the flatten work directory {}: {}", work_dir.string(), ec.message() );
}

} // namespace

MetadataUpdate loadCorpusMetadata( const std::filesystem::path& dir )
{
	const auto cache_path = dir / "ptr_metadata.json";
	{
		std::ifstream file { cache_path };
		if ( file )
		{
			Json::Value root;
			Json::CharReaderBuilder builder;
			std::string errors;
			if ( Json::parseFromStream( builder, file, &root, &errors ) )
			{
				auto metadata = parseMetadataCacheJson( root );
				spdlog::info( "Loaded metadata from {}: {} update indices", cache_path.string(), metadata.updates.size() );
				return metadata;
			}
			spdlog::warn( "Failed to parse {}: {}", cache_path.string(), errors );
		}
	}

	const auto raw_path = dir / "metadata.ptrupdate";
	if ( std::filesystem::exists( raw_path ) )
	{
		try
		{
			auto parsed = parseUpdateFile( raw_path );
			if ( auto* const metadata = std::get_if< MetadataUpdate >( &parsed ) )
			{
				spdlog::info( "Loaded metadata from {}: {} update indices", raw_path.string(), metadata->updates.size() );
				return std::move( *metadata );
			}
			spdlog::warn( "{} did not parse as a MetadataUpdate", raw_path.string() );
		}
		catch ( const std::exception& e )
		{
			spdlog::warn( "Failed to parse {}: {}", raw_path.string(), e.what() );
		}
	}

	throw std::runtime_error(
		std::format( "No PTR metadata found in {}. Run the download step first.", dir.string() ) );
}

FlattenOutcome runFlatten( const std::filesystem::path& ptr_dir,
                           const std::filesystem::path& out_dir,
                           const FlattenCallbacks& callbacks,
                           const std::size_t max_records_per_chunk )
{
	FlattenOutcome outcome {};

	const auto work_dir = out_dir / WORK_SUBDIRECTORY;

	try
	{
		std::filesystem::create_directories( out_dir );

		// Fail before spending hours, not after filling the disk mid-spill.
		const auto space = std::filesystem::space( out_dir );
		if ( space.available < REQUIRED_FREE_BYTES )
		{
			outcome.message = std::format(
				"Not enough free space at {}: {} GiB available, {} GiB required",
				out_dir.string(),
				space.available / ( 1024ULL * 1024 * 1024 ),
				REQUIRED_FREE_BYTES / ( 1024ULL * 1024 * 1024 ) );
			spdlog::error( "{}", outcome.message );
			return outcome;
		}

		announce( callbacks, "Loading metadata" );
		const auto metadata = loadCorpusMetadata( ptr_dir );

		announce( callbacks, "Scanning update files" );
		ScanCallbacks scan_callbacks {};
		scan_callbacks.cancelled = callbacks.cancelled;
		scan_callbacks.progress = callbacks.progress;

		const auto scan = scanCorpus( ptr_dir, metadata, work_dir, scan_callbacks );

		if ( scan.cancelled )
		{
			outcome.cancelled = true;
			outcome.message = "Cancelled during scan";
			removeWorkDirectory( work_dir );
			return outcome;
		}

		announce( callbacks, "Collapsing chains" );

		CollapseResult collapse {};
		RelationsFileStats relation_stats {};

		{
			// Scoped so the mmap is released before the work directory is removed.
			const DefinitionReader definitions { work_dir };

			CollapseCallbacks collapse_callbacks {};
			collapse_callbacks.cancelled = callbacks.cancelled;
			collapse_callbacks.progress = callbacks.progress;

			collapse = collapseBuckets( work_dir, out_dir, definitions, max_records_per_chunk, collapse_callbacks );

			if ( collapse.cancelled )
			{
				outcome.cancelled = true;
				outcome.message = "Cancelled during collapse";
				removeWorkDirectory( work_dir );
				return outcome;
			}

			announce( callbacks, "Writing relations" );

			const auto parents = collapseRelations( scan.parents );
			const auto siblings = collapseRelations( scan.siblings );

			const TagLookup lookup = [ &definitions ]( const std::uint32_t tag_id ) { return definitions.tag( tag_id ); };
			relation_stats = writeRelationsFile( out_dir / RELATIONS_FILENAME, parents, siblings, lookup );
		}

		outcome.manifest.format_version = CHUNK_FORMAT_VERSION;
		outcome.manifest.first_update_index = scan.first_update_index;
		outcome.manifest.last_update_index = scan.last_update_index;
		outcome.manifest.max_records_per_chunk = max_records_per_chunk;
		outcome.manifest.relations_file = RELATIONS_FILENAME;
		outcome.manifest.chunks = collapse.chunks;
		outcome.manifest.stats = collapse.stats;
		outcome.manifest.stats.skipped_files = scan.skipped_files;
		outcome.manifest.stats.skipped_missing_definitions += relation_stats.missing_definitions;

		// Written last. Until this exists the directory is not a compacted directory.
		announce( callbacks, "Writing manifest" );
		writeManifest( out_dir, outcome.manifest );

		removeWorkDirectory( work_dir );

		outcome.success = true;
		outcome.message = std::format(
			"Flattened {} events into {} mappings across {} chunks",
			outcome.manifest.stats.events_scanned,
			outcome.manifest.stats.mappings_after_collapse,
			outcome.manifest.chunks.size() );

		spdlog::info( "{}", outcome.message );
	}
	catch ( const std::exception& e )
	{
		outcome.success = false;
		outcome.message = e.what();
		spdlog::error( "Flatten failed: {}", e.what() );
		removeWorkDirectory( work_dir );
	}

	return outcome;
}

} // namespace idhan::hydrus::ptr
```

- [ ] **Step 5: Run the tests and verify they pass**

```bash
cmake --build build/debug --target IDHANCoreTests -j$(nproc)
./build/debug/bin/IDHANCoreTests --gtest_filter='RunFlattenTest.*'
```

Expected: 9 tests, all PASS.

`MissingMetadataFails` and the disk-space guard interact: if the machine running the tests has under 60 GiB free, every test in this suite fails on the space check rather than on its actual subject. If that happens, note it and temporarily lower `REQUIRED_FREE_BYTES` to confirm the rest passes — do not remove the guard.

- [ ] **Step 6: Commit**

```bash
git add tools/HydrusImporter/ptr-core/include/ptr/flatten/RunFlatten.hpp tools/HydrusImporter/ptr-core/src/RunFlatten.cpp tests/core/ptr/runFlatten.cpp
git commit -m "feat: add runFlatten, the end-to-end PTR compaction pipeline

Scan, collapse, relations, manifest. The manifest is written last so a
cancelled or crashed run leaves a directory that is not recognised as
compacted and therefore cannot be half-imported.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 12: Flatten worker, widget, and tab

The Qt layer. Thin by design — all logic lives in `runFlatten`, so this is signal plumbing following the shape `PTRImportWorker` and `PTRImportWidget` already establish.

**Files:**
- Create: `tools/HydrusImporter/src/ptr/PTRFlattenWorker.hpp`
- Create: `tools/HydrusImporter/src/ptr/PTRFlattenWorker.cpp`
- Create: `tools/HydrusImporter/src/ptr/gui/PTRFlattenWidget.hpp`
- Create: `tools/HydrusImporter/src/ptr/gui/PTRFlattenWidget.cpp`
- Create: `tools/HydrusImporter/src/ptr/gui/PTRFlattenWidget.ui`
- Modify: `tools/HydrusImporter/src/gui/main/MainWindow.cpp:168-180`

**Interfaces:**
- Consumes: `runFlatten`, `FlattenCallbacks`, `FlattenOutcome` (Task 11).
- Produces: `class PTRFlattenWorker : public QObject, public QRunnable` with signals `progress( const QString& )`, `subProgress( int, int, const QString& )`, `finished( bool, const QString& )` and method `requestCancel()`; `class PTRFlattenWidget : public QWidget` with slot `setDirectory( const QString& )` and signal `outputDirectoryChanged( const QString& )`.

- [ ] **Step 1: Write the worker header**

Create `tools/HydrusImporter/src/ptr/PTRFlattenWorker.hpp`:

```cpp
#pragma once

#include <QObject>
#include <QRunnable>
#include <QString>

#include <atomic>
#include <filesystem>

namespace idhan::hydrus::ptr
{

//! QRunnable wrapper around runFlatten. Deliberately thin: every decision lives in PTRCore, so
//! the pipeline stays testable without Qt.
class PTRFlattenWorker : public QObject, public QRunnable
{
	Q_OBJECT

  public:

	PTRFlattenWorker( std::filesystem::path ptr_directory,
	                  std::filesystem::path output_directory,
	                  QObject* parent = nullptr );

	Q_DISABLE_COPY_MOVE( PTRFlattenWorker )
	~PTRFlattenWorker() override;

	void run() override;

	void requestCancel() { m_cancelled = true; }

  signals:

	void progress( const QString& status );
	void subProgress( int current, int total, const QString& status );
	void finished( bool success, const QString& message );

  private:

	std::filesystem::path m_ptr_directory;
	std::filesystem::path m_output_directory;
	std::atomic< bool > m_cancelled { false };
};

} // namespace idhan::hydrus::ptr
```

- [ ] **Step 2: Write the worker implementation**

Create `tools/HydrusImporter/src/ptr/PTRFlattenWorker.cpp`:

```cpp
#include "PTRFlattenWorker.hpp"

#include <QLocale>

#include <spdlog/spdlog.h>

#include <utility>

#include "ptr/flatten/RunFlatten.hpp"

namespace idhan::hydrus::ptr
{

PTRFlattenWorker::PTRFlattenWorker( std::filesystem::path ptr_directory,
                                    std::filesystem::path output_directory,
                                    QObject* parent ) :
  QObject( parent ),
  QRunnable(),
  m_ptr_directory( std::move( ptr_directory ) ),
  m_output_directory( std::move( output_directory ) )
{
	setAutoDelete( false );
}

PTRFlattenWorker::~PTRFlattenWorker() = default;

void PTRFlattenWorker::run()
{
	spdlog::info(
		"PTR flatten worker started: {} -> {}", m_ptr_directory.string(), m_output_directory.string() );

	FlattenCallbacks callbacks {};
	callbacks.cancelled = [ this ] { return m_cancelled.load(); };
	callbacks.stage = [ this ]( const std::string_view text )
	{ emit progress( QString::fromUtf8( text.data(), static_cast< qsizetype >( text.size() ) ) ); };
	callbacks.progress = [ this ]( const std::size_t done, const std::size_t total, const std::string_view text )
	{
		emit subProgress(
			static_cast< int >( done ),
			static_cast< int >( total ),
			QString( "%1 (%2 / %3)" )
				.arg( QString::fromUtf8( text.data(), static_cast< qsizetype >( text.size() ) ) )
				.arg( QLocale::system().toString( static_cast< qlonglong >( done ) ) )
				.arg( QLocale::system().toString( static_cast< qlonglong >( total ) ) ) );
	};

	const auto outcome = runFlatten( m_ptr_directory, m_output_directory, callbacks );

	if ( outcome.cancelled )
	{
		emit finished( false, "Cancelled" );
		return;
	}

	emit finished( outcome.success, QString::fromStdString( outcome.message ) );
}

} // namespace idhan::hydrus::ptr
```

- [ ] **Step 3: Write the widget UI file**

Create `tools/HydrusImporter/src/ptr/gui/PTRFlattenWidget.ui`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<ui version="4.0">
 <class>PTRFlattenWidget</class>
 <widget class="QWidget" name="PTRFlattenWidget">
  <property name="windowTitle">
   <string>Flatten</string>
  </property>
  <layout class="QVBoxLayout" name="rootLayout">
   <item>
    <layout class="QHBoxLayout" name="sourceLayout">
     <item>
      <widget class="QLabel" name="sourceLabel">
       <property name="text">
        <string>PTR files:</string>
       </property>
      </widget>
     </item>
     <item>
      <widget class="QLineEdit" name="sourcePath"/>
     </item>
     <item>
      <widget class="QToolButton" name="selectSource">
       <property name="text">
        <string>...</string>
       </property>
      </widget>
     </item>
    </layout>
   </item>
   <item>
    <layout class="QHBoxLayout" name="outputLayout">
     <item>
      <widget class="QLabel" name="outputLabel">
       <property name="text">
        <string>Output:</string>
       </property>
      </widget>
     </item>
     <item>
      <widget class="QLineEdit" name="outputPath"/>
     </item>
     <item>
      <widget class="QToolButton" name="selectOutput">
       <property name="text">
        <string>...</string>
       </property>
      </widget>
     </item>
    </layout>
   </item>
   <item>
    <widget class="QLabel" name="statusLabel">
     <property name="text">
      <string>Idle</string>
     </property>
     <property name="wordWrap">
      <bool>true</bool>
     </property>
    </widget>
   </item>
   <item>
    <widget class="QProgressBar" name="progressBar">
     <property name="value">
      <number>0</number>
     </property>
    </widget>
   </item>
   <item>
    <layout class="QHBoxLayout" name="buttonLayout">
     <item>
      <widget class="QPushButton" name="flattenButton">
       <property name="text">
        <string>Flatten</string>
       </property>
      </widget>
     </item>
     <item>
      <widget class="QPushButton" name="cancelButton">
       <property name="text">
        <string>Cancel</string>
       </property>
      </widget>
     </item>
    </layout>
   </item>
   <item>
    <spacer name="verticalSpacer">
     <property name="orientation">
      <enum>Qt::Vertical</enum>
     </property>
    </spacer>
   </item>
  </layout>
 </widget>
 <resources/>
 <connections/>
</ui>
```

- [ ] **Step 4: Write the widget header**

Create `tools/HydrusImporter/src/ptr/gui/PTRFlattenWidget.hpp`:

```cpp
#pragma once

#include <QWidget>

#include <memory>

namespace Ui
{
class PTRFlattenWidget;
}

namespace idhan::hydrus::ptr
{
class PTRFlattenWorker;
} // namespace idhan::hydrus::ptr

//! Runs the flatten pass over a downloaded PTR corpus and reports progress. On success it
//! announces the output directory so the Import tab can be pointed straight at it.
class PTRFlattenWidget : public QWidget
{
	Q_OBJECT

  public:

	explicit PTRFlattenWidget( QWidget* parent = nullptr );

	Q_DISABLE_COPY_MOVE( PTRFlattenWidget )
	~PTRFlattenWidget() override;

  public slots:

	void setDirectory( const QString& path );

  signals:

	void outputDirectoryChanged( const QString& path );

  private slots:

	void onSelectSource();
	void onSelectOutput();
	void onFlatten();
	void onCancel();
	void onProgress( const QString& status );
	void onSubProgress( int current, int total, const QString& status );
	void onFinished( bool success, const QString& message );

  private:

	Ui::PTRFlattenWidget* ui;
	std::unique_ptr< idhan::hydrus::ptr::PTRFlattenWorker > m_worker;
	bool m_flattening { false };
};
```

- [ ] **Step 5: Write the widget implementation**

Create `tools/HydrusImporter/src/ptr/gui/PTRFlattenWidget.cpp`:

```cpp
#include "PTRFlattenWidget.hpp"

#include <QFileDialog>
#include <QStandardPaths>
#include <QThreadPool>

#include "ptr/PTRFlattenWorker.hpp"
#include "ui_PTRFlattenWidget.h"

PTRFlattenWidget::PTRFlattenWidget( QWidget* parent ) : QWidget( parent ), ui( new Ui::PTRFlattenWidget )
{
	ui->setupUi( this );

	const QString downloads = QStandardPaths::writableLocation( QStandardPaths::DownloadLocation );
	ui->sourcePath->setText( downloads + "/ptrfiles" );
	ui->outputPath->setText( downloads + "/ptrfiles-compact" );

	ui->flattenButton->setEnabled( true );
	ui->cancelButton->setEnabled( false );

	connect( ui->selectSource, &QToolButton::clicked, this, &PTRFlattenWidget::onSelectSource );
	connect( ui->selectOutput, &QToolButton::clicked, this, &PTRFlattenWidget::onSelectOutput );
	connect( ui->flattenButton, &QPushButton::clicked, this, &PTRFlattenWidget::onFlatten );
	connect( ui->cancelButton, &QPushButton::clicked, this, &PTRFlattenWidget::onCancel );
}

PTRFlattenWidget::~PTRFlattenWidget()
{
	if ( m_worker )
	{
		m_worker->requestCancel();
		m_worker->disconnect();
		QThreadPool::globalInstance()->waitForDone();
	}
	delete ui;
}

void PTRFlattenWidget::setDirectory( const QString& path )
{
	ui->sourcePath->setText( path );
}

void PTRFlattenWidget::onSelectSource()
{
	const QString dir = QFileDialog::getExistingDirectory(
		this, "Select PTR files directory", ui->sourcePath->text(), QFileDialog::ShowDirsOnly );
	if ( !dir.isEmpty() ) ui->sourcePath->setText( dir );
}

void PTRFlattenWidget::onSelectOutput()
{
	const QString dir = QFileDialog::getExistingDirectory(
		this, "Select output directory", ui->outputPath->text(), QFileDialog::ShowDirsOnly );
	if ( !dir.isEmpty() ) ui->outputPath->setText( dir );
}

void PTRFlattenWidget::onFlatten()
{
	const auto source = ui->sourcePath->text();
	const auto output = ui->outputPath->text();
	if ( m_flattening || source.isEmpty() || output.isEmpty() ) return;

	m_flattening = true;
	ui->flattenButton->setEnabled( false );
	ui->cancelButton->setEnabled( true );
	ui->progressBar->setValue( 0 );
	ui->statusLabel->setStyleSheet( "" );
	ui->statusLabel->setText( "Starting..." );

	m_worker = std::make_unique< idhan::hydrus::ptr::PTRFlattenWorker >( source.toStdString(), output.toStdString() );

	connect( m_worker.get(), &idhan::hydrus::ptr::PTRFlattenWorker::progress, this, &PTRFlattenWidget::onProgress );
	connect(
		m_worker.get(), &idhan::hydrus::ptr::PTRFlattenWorker::subProgress, this, &PTRFlattenWidget::onSubProgress );
	connect( m_worker.get(), &idhan::hydrus::ptr::PTRFlattenWorker::finished, this, &PTRFlattenWidget::onFinished );

	QThreadPool::globalInstance()->start( m_worker.get() );
}

void PTRFlattenWidget::onCancel()
{
	if ( m_worker ) m_worker->requestCancel();
}

void PTRFlattenWidget::onProgress( const QString& status )
{
	ui->statusLabel->setText( status );
}

void PTRFlattenWidget::onSubProgress( const int current, const int total, const QString& status )
{
	ui->statusLabel->setText( status );
	ui->progressBar->setMaximum( total );
	ui->progressBar->setValue( current );
}

void PTRFlattenWidget::onFinished( const bool success, const QString& message )
{
	m_flattening = false;
	ui->flattenButton->setEnabled( true );
	ui->cancelButton->setEnabled( false );

	ui->statusLabel->setText( message );
	ui->statusLabel->setStyleSheet( success ? "QLabel { color: green; }" : "QLabel { color: red; }" );

	if ( success ) emit outputDirectoryChanged( ui->outputPath->text() );
}
```

- [ ] **Step 6: Register the tab**

In `tools/HydrusImporter/src/gui/main/MainWindow.cpp`, add the include beside the existing PTR includes at line 21-22:

```cpp
#include "ptr/gui/PTRFlattenWidget.hpp"
```

Replace the body of `on_actionImport_PTR_triggered` at lines 168-180 with:

```cpp
void MainWindow::on_actionImport_PTR_triggered()
{
	auto* ptr_tabs = new QTabWidget( this );
	auto* download_widget = new PTRDownloadWidget( ptr_tabs );
	auto* flatten_widget = new PTRFlattenWidget( ptr_tabs );
	auto* import_widget = new PTRImportWidget( ptr_tabs );
	ptr_tabs->addTab( download_widget, "Download" );
	ptr_tabs->addTab( flatten_widget, "Flatten" );
	ptr_tabs->addTab( import_widget, "Import" );

	connect( download_widget, &PTRDownloadWidget::directoryChanged, import_widget, &PTRImportWidget::setDirectory );
	connect( download_widget, &PTRDownloadWidget::directoryChanged, flatten_widget, &PTRFlattenWidget::setDirectory );

	// A finished flatten points the Import tab at the compacted output, so the normal path is
	// Download, Flatten, Import without retyping a directory.
	connect( flatten_widget, &PTRFlattenWidget::outputDirectoryChanged, import_widget, &PTRImportWidget::setDirectory );

	ui->importTabs->addTab( ptr_tabs, "PTR Importer" );
	ui->importTabs->setCurrentIndex( ui->importTabs->count() - 1 );
}
```

- [ ] **Step 7: Build and verify**

```bash
cmake --build build/debug --target HydrusImporter -j$(nproc)
```

Expected: success. A missing `ui_PTRFlattenWidget.h` means the `.ui` file was not picked up — the `GLOB_RECURSE` for `**.ui` in `tools/HydrusImporter/CMakeLists.txt` is `CONFIGURE_DEPENDS`, so re-run `cmake build/debug` to force a reconfigure.

- [ ] **Step 8: Ask the user to smoke-test**

Do not launch the GUI yourself. Ask the user to run `./build/debug/bin/HydrusImporter`, open Options then Import PTR, and confirm: three tabs appear (Download, Flatten, Import); the Flatten tab prefills source and output paths; pressing Flatten against a directory with no metadata reports a red error rather than hanging; Cancel during a real run stops it. Report back what they observe.

- [ ] **Step 9: Commit**

```bash
git add tools/HydrusImporter/src/ptr/PTRFlattenWorker.hpp tools/HydrusImporter/src/ptr/PTRFlattenWorker.cpp tools/HydrusImporter/src/ptr/gui/PTRFlattenWidget.hpp tools/HydrusImporter/src/ptr/gui/PTRFlattenWidget.cpp tools/HydrusImporter/src/ptr/gui/PTRFlattenWidget.ui tools/HydrusImporter/src/gui/main/MainWindow.cpp
git commit -m "feat: add PTR Flatten tab

Thin Qt wrapper over runFlatten, following the existing download and import
widget shape. A finished flatten points the Import tab at its output.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

**Remaining task (13) continues in:** `docs/superpowers/plans/2026-07-30-ptr-flattener-part5.md`
