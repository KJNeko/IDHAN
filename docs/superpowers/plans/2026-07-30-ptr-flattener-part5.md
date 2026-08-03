# PTR Flattener Implementation Plan — Part 5 (Task 13)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax for tracking.

Continues `docs/superpowers/plans/2026-07-30-ptr-flattener-part4.md`. The **Global Constraints** in part 1 apply.

---

### Task 13: Compacted import path

The payoff. `PTRImportWorker` learns to recognise a compacted directory and import chunks instead of update files, holding one flat `ptr_tag_id -> TagID` array (188 MB) plus one chunk — flat for the whole run, against the raw path's 20 GB and climbing.

Detection is by `compact_manifest.json`, so the two paths never ambiguate: the flattener writes that file last, and a partial output has none.

**Files:**
- Create: `tools/HydrusImporter/ptr-core/include/ptr/flatten/HexEncode.hpp`
- Create: `tools/HydrusImporter/ptr-core/src/HexEncode.cpp`
- Modify: `tools/HydrusImporter/src/ptr/PTRImportWorker.hpp` (new members and methods)
- Modify: `tools/HydrusImporter/src/ptr/PTRImportWorker.cpp:103-185` (run and loadMetadata), plus new methods
- Test: `tests/core/ptr/hexEncode.cpp`

**Interfaces:**
- Consumes: `readChunk`, `Chunk` (Task 6); `readManifest`, `isCompactedDirectory`, `CompactManifest` (Task 7); `loadCorpusMetadata` (Task 11); `IDHANClient::createTags`, `createRecords`, `addTags`, `removeTags`.
- Produces: `std::string toHex( std::span< const std::byte > )`; `PTRImportWorker::processCompacted()`, `PTRImportWorker::importChunk(...)`, `PTRImportWorker::resolveChunkTags(...)`.

- [ ] **Step 1: Write the failing hex test**

Create `tests/core/ptr/hexEncode.cpp`:

```cpp
//
// Created by kj16609 on 7/30/26.
//

#include <gtest/gtest.h>

#include <array>
#include <cstddef>

#include "ptr/flatten/DefinitionStore.hpp"
#include "ptr/flatten/HexEncode.hpp"

namespace
{

using namespace idhan::hydrus::ptr;

TEST( PTRHexEncode, EncodesAllZeroes )
{
	const std::array< std::byte, SHA256_BYTES > bytes {};
	EXPECT_EQ( toHex( bytes ), std::string( 64, '0' ) );
}

TEST( PTRHexEncode, EncodesLowercaseWithLeadingZeroNibbles )
{
	std::array< std::byte, 4 > bytes { std::byte { 0x0A }, std::byte { 0xB0 }, std::byte { 0xFF }, std::byte { 0x00 } };
	EXPECT_EQ( toHex( bytes ), "0ab0ff00" );
}

TEST( PTRHexEncode, RoundTripsThroughDecodeSha256Hex )
{
	constexpr std::string_view original { "f69a2836e6ab4e089b9a6695c3d65d2da02f8d69737135e0cec45e173aaafdcd" };
	const auto decoded = decodeSha256Hex( original );
	ASSERT_TRUE( decoded.has_value() );
	EXPECT_EQ( toHex( *decoded ), original );
}

TEST( PTRHexEncode, EmptyInputEncodesEmpty )
{
	EXPECT_TRUE( toHex( {} ).empty() );
}

} // namespace
```

- [ ] **Step 2: Run it and verify it fails**

```bash
cmake --build build/debug --target IDHANCoreTests -j$(nproc)
```

Expected: FAIL, `fatal error: ptr/flatten/HexEncode.hpp: No such file or directory`.

- [ ] **Step 3: Write the hex helper**

Create `tools/HydrusImporter/ptr-core/include/ptr/flatten/HexEncode.hpp`:

```cpp
#pragma once

#include <cstddef>
#include <span>
#include <string>

namespace idhan::hydrus::ptr
{

//! Lowercase hex, two characters per byte. Chunks store hashes as raw bytes; the record API
//! takes them as hex, so this is the boundary conversion.
std::string toHex( std::span< const std::byte > bytes );

} // namespace idhan::hydrus::ptr
```

Create `tools/HydrusImporter/ptr-core/src/HexEncode.cpp`:

```cpp
#include "ptr/flatten/HexEncode.hpp"

namespace idhan::hydrus::ptr
{

std::string toHex( const std::span< const std::byte > bytes )
{
	constexpr char DIGITS[] = "0123456789abcdef";

	std::string out;
	out.reserve( bytes.size() * 2 );

	for ( const auto byte : bytes )
	{
		const auto value = std::to_integer< unsigned >( byte );
		out.push_back( DIGITS[ ( value >> 4 ) & 0xF ] );
		out.push_back( DIGITS[ value & 0xF ] );
	}

	return out;
}

} // namespace idhan::hydrus::ptr
```

- [ ] **Step 4: Run the hex tests and verify they pass**

```bash
cmake --build build/debug --target IDHANCoreTests -j$(nproc)
./build/debug/bin/IDHANCoreTests --gtest_filter='PTRHexEncode.*'
```

Expected: 4 tests, all PASS.

- [ ] **Step 5: Commit the helper**

```bash
git add tools/HydrusImporter/ptr-core/include/ptr/flatten/HexEncode.hpp tools/HydrusImporter/ptr-core/src/HexEncode.cpp tests/core/ptr/hexEncode.cpp
git commit -m "feat: add hex encoding for chunk record hashes

Chunks store hashes as raw bytes; the record API takes hex.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

- [ ] **Step 6: Extend the worker header**

In `tools/HydrusImporter/src/ptr/PTRImportWorker.hpp`, add these includes beside the existing ones:

```cpp
#include "ptr/flatten/ChunkFormat.hpp"
#include "ptr/flatten/Manifest.hpp"
```

Add to the `private:` section, after the existing `processSingleContentFile` declaration:

```cpp
	//! Imports every chunk the manifest lists. Returns true if cancelled mid-run.
	bool processCompacted( idhan::TagDomainID domain_id );

	//! Creates any tag in \p chunk's string table not yet in m_ptr_tag_to_idhan, then returns the
	//! chunk's local string indices resolved to IDHAN TagIDs. Index i of the result corresponds to
	//! chunk.strings[i]; an entry is 0 if the tag could not be created.
	std::vector< idhan::TagID > resolveChunkTags( const Chunk& chunk, const QString& progress_prefix );

	ContentStats importChunk( const Chunk& chunk, idhan::TagDomainID domain_id, const QString& progress_prefix );

	void loadImportedChunks();
	void saveImportedChunks() const;
```

And to the member section, after `m_imported_hashes`:

```cpp
	//! True when the directory holds compacted output rather than raw update files.
	bool m_compacted { false };
	CompactManifest m_manifest;

	//! ptr_tag_id -> IDHAN TagID, 0 meaning not yet created. A flat array rather than a map: at
	//! PTR's 47M tag ids this is 188 MB of TagIDs with no strings, and it is what lets each tag be
	//! created exactly once even though every chunk carries its own copy of the text.
	std::vector< idhan::TagID > m_ptr_tag_to_idhan;

	std::unordered_set< std::string > m_imported_chunks;
```

- [ ] **Step 7: Add detection to `loadMetadata`**

In `tools/HydrusImporter/src/ptr/PTRImportWorker.cpp`, replace the body of `loadMetadata` (lines 129-185) with:

```cpp
void PTRImportWorker::loadMetadata()
{
	// A compacted directory is marked by its manifest, which the flattener writes last. A partial
	// or cancelled flatten therefore never looks importable.
	if ( isCompactedDirectory( m_ptr_directory ) )
	{
		m_compacted = true;
		m_manifest = readManifest( m_ptr_directory );
		loadImportedChunks();

		spdlog::info(
			"Loaded compacted manifest: {} chunks, {} previously imported",
			m_manifest.chunks.size(),
			m_imported_chunks.size() );
		return;
	}

	m_metadata = loadCorpusMetadata( m_ptr_directory );

	// The downloader records which raw files it has already handed over.
	const auto meta_json_path = m_ptr_directory / "ptr_metadata.json";
	std::ifstream file( meta_json_path );
	if ( file )
	{
		Json::Value root;
		Json::CharReaderBuilder builder;
		std::string errors;
		if ( Json::parseFromStream( builder, file, &root, &errors ) )
		{
			const auto& imported = root[ "imported_files" ];
			if ( imported.isObject() )
			{
				for ( const auto& hash : imported.getMemberNames() )
				{
					if ( imported[ hash ].asBool() ) m_imported_hashes.insert( hash );
				}
			}
		}
	}

	spdlog::info(
		"Loaded metadata: {} update indices, {} previously imported",
		m_metadata.updates.size(),
		m_imported_hashes.size() );
}
```

Add these includes to the file's include block:

```cpp
#include "ptr/flatten/ChunkFormat.hpp"
#include "ptr/flatten/HexEncode.hpp"
#include "ptr/flatten/Manifest.hpp"
#include "ptr/flatten/RunFlatten.hpp"
```

- [ ] **Step 8: Branch in `run`**

Replace lines 112-117 of `run` with:

```cpp
		emit progress( m_compacted ? "Importing compacted chunks..." : "Processing files in metadata order..." );

		if ( m_compacted )
		{
			auto& client = IDHANClient::instance();
			const std::string domain_name = "public tag repository";
			TagDomainID domain_id = 0;

			auto existing = client.getTagDomain( domain_name );
			existing.waitForFinished();
			if ( const auto result = existing.result(); result.has_value() )
			{
				domain_id = result.value();
			}
			else
			{
				auto created = client.createTagDomain( domain_name );
				created.waitForFinished();
				domain_id = created.result();
			}

			if ( domain_id == TagDomainID( 0 ) )
				throw std::runtime_error( "Failed to get or create tag domain '" + domain_name + "'" );

			if ( processCompacted( domain_id ) )
			{
				emit finished( false, "Cancelled" );
				return;
			}
		}
		else if ( processInOrder() )
		{
			emit finished( false, "Cancelled" );
			return;
		}
```

- [ ] **Step 9: Implement the compacted path**

Append these methods to `tools/HydrusImporter/src/ptr/PTRImportWorker.cpp`, inside `namespace idhan::hydrus::ptr`, before the closing brace:

```cpp
void PTRImportWorker::loadImportedChunks()
{
	std::ifstream file( m_ptr_directory / "imported_chunks.json" );
	if ( !file ) return;

	Json::Value root;
	Json::CharReaderBuilder builder;
	std::string errors;
	if ( !Json::parseFromStream( builder, file, &root, &errors ) )
	{
		spdlog::warn( "Failed to parse imported_chunks.json: {}", errors );
		return;
	}

	if ( !root.isArray() ) return;
	for ( const auto& entry : root )
	{
		if ( entry.isString() ) m_imported_chunks.insert( entry.asString() );
	}
}

void PTRImportWorker::saveImportedChunks() const
{
	// Seeded as an array so an empty set serialises as [] rather than null.
	Json::Value root { Json::arrayValue };
	for ( const auto& chunk : m_imported_chunks ) root.append( chunk );

	std::ofstream file( m_ptr_directory / "imported_chunks.json", std::ios::trunc );
	if ( !file )
	{
		spdlog::warn( "Failed to write imported_chunks.json; resume will redo work" );
		return;
	}

	Json::StreamWriterBuilder builder;
	file << Json::writeString( builder, root );
}

std::vector< TagID > PTRImportWorker::resolveChunkTags( const Chunk& chunk, const QString& progress_prefix )
{
	auto& client = IDHANClient::instance();

	std::vector< TagID > resolved( chunk.strings.size(), TagID( 0 ) );

	// Only the ids never seen before need creating. Across the corpus this makes each tag exactly
	// one createTags row, even though hundreds of chunks carry its text.
	std::vector< std::size_t > unseen;
	std::vector< std::pair< std::string, std::string > > to_create;

	for ( std::size_t i = 0; i < chunk.strings.size(); ++i )
	{
		const auto ptr_tag_id = chunk.strings[ i ].ptr_tag_id;

		if ( ptr_tag_id >= m_ptr_tag_to_idhan.size() ) m_ptr_tag_to_idhan.resize( ptr_tag_id + 1, TagID( 0 ) );

		if ( const auto known = m_ptr_tag_to_idhan[ ptr_tag_id ]; known != TagID( 0 ) )
		{
			resolved[ i ] = known;
			continue;
		}

		unseen.push_back( i );
		to_create.push_back( splitTag( chunk.strings[ i ].tag ) );
	}

	if ( to_create.empty() ) return resolved;

	auto futures = launchBatches(
		to_create.size(),
		BATCH_SIZE,
		[ & ]( const std::size_t s, const std::size_t e )
		{
			return client.createTags( std::vector< std::pair< std::string, std::string > >(
				to_create.begin() + static_cast< std::ptrdiff_t >( s ),
				to_create.begin() + static_cast< std::ptrdiff_t >( e ) ) );
		} );

	awaitBatches(
		futures,
		BATCH_SIZE,
		"createTags",
		[ & ]( const std::size_t i, const TagID tag_id )
		{
			if ( i >= unseen.size() ) return;
			const auto string_index = unseen[ i ];
			resolved[ string_index ] = tag_id;
			m_ptr_tag_to_idhan[ chunk.strings[ string_index ].ptr_tag_id ] = tag_id;
		},
		[ & ]( const std::size_t done, const std::size_t total )
		{
			emit subProgress( static_cast< int >( done ), static_cast< int >( total ), progress_prefix + " - creating tags" );
		} );

	return resolved;
}

ContentStats PTRImportWorker::importChunk( const Chunk& chunk, const TagDomainID domain_id, const QString& progress_prefix )
{
	ContentStats stats;
	auto& client = IDHANClient::instance();

	const auto resolved = resolveChunkTags( chunk, progress_prefix );
	if ( m_cancelled ) return stats;

	std::vector< std::string > hashes;
	hashes.reserve( chunk.records.size() );
	for ( const auto& record : chunk.records ) hashes.push_back( toHex( record.sha256 ) );

	// Index-parallel to chunk.records, so no hash-to-record map is ever built.
	std::vector< RecordID > record_ids( hashes.size(), RecordID( 0 ) );

	{
		auto futures = launchBatches(
			hashes.size(),
			BATCH_SIZE,
			[ & ]( const std::size_t s, const std::size_t e )
			{
				return client.createRecords( std::vector< std::string >(
					hashes.begin() + static_cast< std::ptrdiff_t >( s ),
					hashes.begin() + static_cast< std::ptrdiff_t >( e ) ) );
			} );

		awaitBatches(
			futures,
			BATCH_SIZE,
			"createRecords",
			[ & ]( const std::size_t i, const RecordID record_id )
			{
				if ( i >= record_ids.size() ) return;
				record_ids[ i ] = record_id;
				++stats.records_created;
			},
			[ & ]( const std::size_t done, const std::size_t total )
			{
				emit subProgress(
					static_cast< int >( done ), static_cast< int >( total ), progress_prefix + " - creating records" );
			} );
	}

	if ( m_cancelled ) return stats;

	// Build the two (records, tag sets) pairs. A record contributes to a list only if it has
	// something in it, so empty sets never reach the API.
	std::vector< RecordID > add_records;
	std::vector< std::vector< TagID > > add_sets;
	std::vector< RecordID > del_records;
	std::vector< std::vector< TagID > > del_sets;

	const auto gather = [ & ]( const std::vector< std::uint32_t >& indices )
	{
		std::vector< TagID > out;
		out.reserve( indices.size() );
		for ( const auto index : indices )
		{
			if ( index >= resolved.size() ) continue;
			if ( const auto tag_id = resolved[ index ]; tag_id != TagID( 0 ) ) out.push_back( tag_id );
		}
		return out;
	};

	for ( std::size_t i = 0; i < chunk.records.size(); ++i )
	{
		if ( record_ids[ i ] == RecordID( 0 ) ) continue;

		if ( auto adds = gather( chunk.records[ i ].add_indices ); !adds.empty() )
		{
			stats.mappings_added += static_cast< int >( adds.size() );
			add_records.push_back( record_ids[ i ] );
			add_sets.push_back( std::move( adds ) );
		}

		if ( auto dels = gather( chunk.records[ i ].del_indices ); !dels.empty() )
		{
			stats.mappings_removed += static_cast< int >( dels.size() );
			del_records.push_back( record_ids[ i ] );
			del_sets.push_back( std::move( dels ) );
		}
	}

	std::vector< QFuture< void > > ops;
	const auto append = [ &ops ]( auto&& futures )
	{
		ops.insert( ops.end(), std::make_move_iterator( futures.begin() ), std::make_move_iterator( futures.end() ) );
	};

	append( launchBatches(
		add_records.size(),
		BATCH_SIZE,
		[ & ]( const std::size_t s, const std::size_t e )
		{
			std::vector< RecordID > br(
				add_records.begin() + static_cast< std::ptrdiff_t >( s ),
				add_records.begin() + static_cast< std::ptrdiff_t >( e ) );
			std::vector< std::vector< TagID > > bs(
				std::make_move_iterator( add_sets.begin() + static_cast< std::ptrdiff_t >( s ) ),
				std::make_move_iterator( add_sets.begin() + static_cast< std::ptrdiff_t >( e ) ) );
			return client.addTags( std::move( br ), domain_id, std::move( bs ) );
		} ) );

	append( launchBatches(
		del_records.size(),
		BATCH_SIZE,
		[ & ]( const std::size_t s, const std::size_t e )
		{
			std::vector< RecordID > br(
				del_records.begin() + static_cast< std::ptrdiff_t >( s ),
				del_records.begin() + static_cast< std::ptrdiff_t >( e ) );
			std::vector< std::vector< TagID > > bs(
				std::make_move_iterator( del_sets.begin() + static_cast< std::ptrdiff_t >( s ) ),
				std::make_move_iterator( del_sets.begin() + static_cast< std::ptrdiff_t >( e ) ) );
			return client.removeTags( std::move( br ), domain_id, std::move( bs ) );
		} ) );

	if ( !ops.empty() )
	{
		awaitBatches(
			ops,
			"chunk mappings",
			[ & ]( const std::size_t done, const std::size_t total )
			{
				emit subProgress(
					static_cast< int >( done ), static_cast< int >( total ), progress_prefix + " - applying mappings" );
			} );
	}

	return stats;
}

bool PTRImportWorker::processCompacted( const TagDomainID domain_id )
{
	const auto total = m_manifest.chunks.size();
	std::size_t done { 0 };

	spdlog::info( "Importing {} compacted chunks", total );

	for ( const auto& entry : m_manifest.chunks )
	{
		if ( m_cancelled ) return true;

		++done;

		if ( m_imported_chunks.count( entry.file ) )
		{
			spdlog::debug( "Skipping already-imported chunk: {}", entry.file );
			emit fileProcessed( static_cast< int >( done ), static_cast< int >( total ) );
			continue;
		}

		const auto prefix = QString( "Chunk %1/%2" )
		                        .arg( QLocale::system().toString( static_cast< qlonglong >( done ) ) )
		                        .arg( QLocale::system().toString( static_cast< qlonglong >( total ) ) );

		emit progress( prefix + QString( " - %1" ).arg( QString::fromStdString( entry.file ) ) );

		ContentStats stats;
		try
		{
			const auto chunk = readChunk( m_ptr_directory / entry.file );
			stats = importChunk( chunk, domain_id, prefix );
		}
		catch ( const std::exception& e )
		{
			// One unreadable chunk must not end the import; the rest are independent.
			spdlog::error( "Failed to import chunk {}: {}", entry.file, e.what() );
			emit fileProcessed( static_cast< int >( done ), static_cast< int >( total ) );
			continue;
		}

		if ( m_cancelled ) return true;

		m_imported_chunks.insert( entry.file );
		saveImportedChunks();

		PTRHistoryEntry history_entry;
		history_entry.update_index = static_cast< int >( done );
		history_entry.file_count = static_cast< std::int64_t >( entry.records );
		history_entry.stats = stats;
		emit updateCompleted( history_entry );

		emit fileProcessed( static_cast< int >( done ), static_cast< int >( total ) );
	}

	spdlog::info( "Compacted import complete: {} chunks", total );
	return false;
}
```

- [ ] **Step 10: Build and verify**

```bash
cmake --build build/debug --target HydrusImporter -j$(nproc)
cmake --build build/debug --target IDHANCoreTests -j$(nproc)
./build/debug/bin/IDHANCoreTests --gtest_filter='PTR*:BucketSpillTest.*:DefinitionStoreTest.*:ChunkFormatTest.*:ManifestTest.*:FlattenScanTest.*:FlattenCollapseTest.*:RelationsFileTest.*:RunFlattenTest.*'
```

Expected: both build; every test PASSes.

If `launchBatches` or `awaitBatches` are not visible from the new methods, they are in the anonymous namespace at the top of `PTRImportWorker.cpp` and the new methods must be appended **below** it in the same translation unit.

- [ ] **Step 11: Ask the user to run the real thing**

Do not run the importer yourself. Ask the user to, with a server running:

1. Flatten a subset of `~/Downloads/ptrfiles` into a scratch output directory via the Flatten tab.
2. Point the Import tab at that output and import it.
3. Report: does the Import tab show chunks rather than update files, does memory stay flat (`ps -o rss= -p $(pgrep HydrusImporter)` should hold around 250 MB rather than climbing into the gigabytes), and do tags appear on records in the `public tag repository` domain.
4. Re-run the import to confirm resume skips every chunk and completes immediately.

The memory observation is the headline claim of this whole feature, so get an actual number rather than an impression.

- [ ] **Step 12: Commit**

```bash
git add tools/HydrusImporter/src/ptr/PTRImportWorker.hpp tools/HydrusImporter/src/ptr/PTRImportWorker.cpp
git commit -m "feat: import compacted PTR chunks with flat memory

PTRImportWorker detects compact_manifest.json and imports chunks instead of
update files. A flat ptr_tag_id to TagID array creates each tag exactly once
across the run, so resident memory stays around 250 MB where the raw path
needs over 20 GB for its translation tables.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Plan self-review

**Spec coverage.** Every section of `2026-07-30-ptr-flattener-design.md` maps to a task: scan (8), collapse (9), chunk emission (6, 9), relations (10), manifest (7), definition store (5), collapse rule (3), chunk format (6), import path (13), GUI (12), error handling (8, 9, 11, 13), testing (throughout). The Qt-free-library constraint is task 1.

**Known deviations from the spec, both deliberate:**

1. **Collapse is single-threaded.** The spec calls for parallel collapse across buckets via `QThreadPool`. Task 9 ships it serial, because parallelism needs one `ChunkSink` per worker plus an entry-list merge, and correctness of the chain logic matters more than wall-clock on a one-shot job. Parallelising afterwards is a contained change: give each worker its own sink and concatenate the entry lists. Flagged in Task 9's notes.

2. **`PTRCore` holds the parser, not just the collapse logic.** The spec said the collapse logic should be a separate library. Task 1 moves `PTRFileParser` in as well, because the scan-stage tests need to build and read real `.ptrupdate` files through the production parser rather than a mock.

**Unverifiable-by-test surface.** Tasks 12 and 13 end in user smoke-tests rather than assertions: the Qt layer needs an event loop and the import path needs a live server and PostgreSQL, neither of which `IDHANCoreTests` has. The flatten-then-import-equals-raw-import equivalence the spec wanted as an integration test is therefore only partially covered — `RunFlattenTest` asserts the compacted output is correct, but not that importing it yields the same database state as importing raw. Closing that gap needs a `ServerDBFixture`-based test in `IDHANTests`, which the spec notes has never linked. Worth doing separately; not blocking.
