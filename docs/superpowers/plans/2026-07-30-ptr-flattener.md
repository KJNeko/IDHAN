# PTR Flattener Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Compact the 26,324-file / 20 GB Hydrus PTR corpus into a record-major form that collapses every add/delete chain to a single operation, and import it with flat, bounded memory.

**Architecture:** A Qt-free static library (`PTRCore`) holds the parser and all flatten primitives so the logic is unit-testable without a database or server. Flattening runs in two disk-backed stages: a scan that spills 12-byte mapping events into 4096 hash-partitioned buckets, then a parallel collapse where each bucket is sorted in RAM and chains are collapsed over contiguous spans. Output is ~975 self-contained chunk files consumed by a new branch of `PTRImportWorker`.

**Tech Stack:** C++23, Qt6 (Widgets/Concurrent, GUI layer only), zlib, jsoncpp, spdlog, GoogleTest, CMake with libFGL helpers.

## Global Constraints

- Design spec: `docs/superpowers/specs/2026-07-30-ptr-flattener-design.md`. Read it before starting.
- `PTRCore` must not link Qt. It is linked by both `HydrusImporter` and `IDHANCoreTests`; the latter has no Qt, no drogon, and no PostgreSQL.
- `PTRCore` sources must live **outside** `tools/HydrusImporter/src`. `AddFGLExecutable` glob-recurses that directory, so a library under it would produce duplicate symbols.
- All integer IDs use the `IDHANTypes.hpp` aliases (`RecordID`, `TagID`, `TagDomainID`) — never raw integers. PTR-side ids are `std::uint32_t` and are deliberately distinct from IDHAN ids.
- Namespace for all new PTR code: `idhan::hydrus::ptr`.
- Formatting follows the surrounding code: tabs, `if ( cond )` spacing inside parens, Allman braces, `//!` Doxygen comments.
- No emojis anywhere, including commit messages.
- Build directory: `build/debug` (already configured with `BUILD_IDHAN_TESTS=ON`). Test binary: `build/debug/bin/IDHANCoreTests`.
- Commit after each task. Do not push.

---

### Task 1: Extract the `PTRCore` static library

Moves the existing parser and constants out of the Qt executable into a Qt-free static library, and proves the library is linkable from `IDHANCoreTests`. Everything later builds on this.

**Files:**
- Create: `tools/HydrusImporter/ptr-core/CMakeLists.txt`
- Create: `tools/HydrusImporter/ptr-core/include/ptr/PTRFileParser.hpp` (moved from `tools/HydrusImporter/src/ptr/PTRFileParser.hpp`)
- Create: `tools/HydrusImporter/ptr-core/include/ptr/PTRConstants.hpp` (moved from `tools/HydrusImporter/src/ptr/PTRConstants.hpp`)
- Create: `tools/HydrusImporter/ptr-core/src/PTRFileParser.cpp` (moved from `tools/HydrusImporter/src/ptr/PTRFileParser.cpp`)
- Delete: the three original paths above under `src/ptr/`
- Modify: `CMakeLists.txt` (root, add subdirectory before the tools block at line 91)
- Modify: `tools/HydrusImporter/CMakeLists.txt` (link `PTRCore`)
- Modify: `tests/CMakeLists.txt` (link `PTRCore` into `IDHANCoreTests`)
- Modify: `tools/HydrusImporter/src/ptr/PTRImportWorker.hpp:16`, `tools/HydrusImporter/src/ptr/PTRImportWorker.cpp:18-19`, `tools/HydrusImporter/src/ptr/PTRDownloader.hpp:16`, `tools/HydrusImporter/src/ptr/PTRDownloader.cpp:19-20` (include paths)
- Test: `tests/core/ptr/detectUpdateType.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: CMake target `PTRCore` (STATIC, `PUBLIC` include dir `tools/HydrusImporter/ptr-core/include`). Headers are reached as `#include "ptr/PTRFileParser.hpp"` and `#include "ptr/PTRConstants.hpp"`. All existing free functions in `idhan::hydrus::ptr` keep their current signatures.

- [ ] **Step 1: Move the three files with git mv**

```bash
cd /home/kj16609/Desktop/Projects/cxx/IDHAN
mkdir -p tools/HydrusImporter/ptr-core/include/ptr tools/HydrusImporter/ptr-core/src
git mv tools/HydrusImporter/src/ptr/PTRFileParser.hpp tools/HydrusImporter/ptr-core/include/ptr/PTRFileParser.hpp
git mv tools/HydrusImporter/src/ptr/PTRConstants.hpp  tools/HydrusImporter/ptr-core/include/ptr/PTRConstants.hpp
git mv tools/HydrusImporter/src/ptr/PTRFileParser.cpp tools/HydrusImporter/ptr-core/src/PTRFileParser.cpp
```

- [ ] **Step 2: Fix the include paths in the five referencing files**

In `tools/HydrusImporter/ptr-core/src/PTRFileParser.cpp`, change line 1 and line 13:

```cpp
#include "ptr/PTRFileParser.hpp"
```
```cpp
#include "ptr/PTRConstants.hpp"
```

In `tools/HydrusImporter/src/ptr/PTRImportWorker.hpp:16`, `tools/HydrusImporter/src/ptr/PTRDownloader.hpp:16`:

```cpp
#include "ptr/PTRFileParser.hpp"
```

In `tools/HydrusImporter/src/ptr/PTRImportWorker.cpp:18-19` and `tools/HydrusImporter/src/ptr/PTRDownloader.cpp:19-20`:

```cpp
#include "ptr/PTRConstants.hpp"
#include "ptr/PTRFileParser.hpp"
```

- [ ] **Step 3: Write the library CMakeLists**

Create `tools/HydrusImporter/ptr-core/CMakeLists.txt`:

```cmake
# Qt-free core of the PTR pipeline: update-file parsing plus every flatten primitive.
# Deliberately separate from HydrusImporter so IDHANCoreTests can link it without pulling
# in Qt, drogon, or a live PostgreSQL. Must stay outside HydrusImporter/src, which is
# glob-recursed by AddFGLExecutable.

find_package(ZLIB REQUIRED)

AddFGLLibrary(PTRCore STATIC
		${CMAKE_CURRENT_SOURCE_DIR}/src
		${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(PTRCore PUBLIC IDHAN)
target_link_libraries(PTRCore PRIVATE ZLIB::ZLIB spdlog::spdlog)
```

`IDHAN` is `PUBLIC` because it transitively supplies `Jsoncpp_lib`, and `PTRFileParser.hpp` includes `<json/json.h>`.

- [ ] **Step 4: Wire it into the root build**

In the root `CMakeLists.txt`, insert immediately **before** the `if (BUILD_HYDRUS_IMPORTER OR BUILD_IDHAN_TOOLS)` block at line 91:

```cmake
# Unconditional: PTRCore is Qt-free and IDHANCoreTests links it, so it must exist even when
# the importer itself is not being built.
add_subdirectory(tools/HydrusImporter/ptr-core)
```

- [ ] **Step 5: Link it from the two consumers**

In `tools/HydrusImporter/CMakeLists.txt`, add `PTRCore` to the existing `target_link_libraries` call:

```cmake
target_link_libraries(HydrusImporter PUBLIC Qt6::Core Qt6::Concurrent Qt6::Network IDHANClient sqlite3 spdlog::spdlog Qt6::Widgets ZLIB::ZLIB PTRCore)
```

In `tests/CMakeLists.txt`, add `PTRCore` to the `IDHANCoreTests` link line:

```cmake
target_link_libraries(IDHANCoreTests PUBLIC IDHAN GTest::gtest_main PTRCore)
```

- [ ] **Step 6: Write the failing test**

Create `tests/core/ptr/detectUpdateType.cpp`:

```cpp
//
// Created by kj16609 on 7/30/26.
//

#include <gtest/gtest.h>

#include <json/json.h>

#include "ptr/PTRConstants.hpp"
#include "ptr/PTRFileParser.hpp"

namespace
{

using namespace idhan::hydrus::ptr;

//! A PTR update file's JSON root is [ serialisable_type, version, serialisable_info ].
Json::Value makeRoot( const int serialisable_type )
{
	Json::Value root { Json::arrayValue };
	root.append( serialisable_type );
	root.append( 1 );
	root.append( Json::Value( Json::arrayValue ) );
	return root;
}

TEST( PTRDetectUpdateType, RecognisesContent )
{
	EXPECT_EQ( detectUpdateType( makeRoot( SERIALISABLE_TYPE_CONTENT_UPDATE ) ), UpdateType::Content );
}

TEST( PTRDetectUpdateType, RecognisesDefinitions )
{
	EXPECT_EQ( detectUpdateType( makeRoot( SERIALISABLE_TYPE_DEFINITIONS_UPDATE ) ), UpdateType::Definitions );
}

TEST( PTRDetectUpdateType, RecognisesMetadata )
{
	EXPECT_EQ( detectUpdateType( makeRoot( SERIALISABLE_TYPE_METADATA ) ), UpdateType::Metadata );
}

TEST( PTRDetectUpdateType, UnknownTypeIsUnknown )
{
	EXPECT_EQ( detectUpdateType( makeRoot( 9999 ) ), UpdateType::Unknown );
}

} // namespace
```

- [ ] **Step 7: Run it and confirm it fails to build first, then passes**

```bash
cmake --build build/debug --target IDHANCoreTests -j$(nproc)
./build/debug/bin/IDHANCoreTests --gtest_filter='PTRDetectUpdateType.*'
```

Expected: 4 tests, all PASS. A link error naming `detectUpdateType` means `PTRCore` is not linked into `IDHANCoreTests`; a "file not found" on `ptr/PTRFileParser.hpp` means the `PUBLIC` include directory did not propagate.

- [ ] **Step 8: Confirm the importer still builds**

```bash
cmake --build build/debug --target HydrusImporter -j$(nproc)
```

Expected: success. Duplicate-symbol errors on `readFile` or `decompressToJson` mean a copy of the moved sources is still under `tools/HydrusImporter/src`.

- [ ] **Step 9: Commit**

```bash
git add tools/HydrusImporter/ptr-core tools/HydrusImporter/src/ptr tools/HydrusImporter/CMakeLists.txt CMakeLists.txt tests/CMakeLists.txt tests/core/ptr
git commit -m "refactor: extract Qt-free PTRCore library from HydrusImporter

Moves PTRFileParser and PTRConstants into a static library outside
HydrusImporter/src so IDHANCoreTests can link the PTR pipeline without Qt,
drogon, or a live PostgreSQL. Enables unit testing the flattener primitives
added in subsequent commits.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 2: `MappingEvent`, bucket routing, and sort order

The 12-byte spill record and its total order. The sort order is load-bearing: `op` sorts last so that a DELETE issued in the same update index as an ADD lands after it, matching how `processSingleContentFile` applies them today.

**Files:**
- Create: `tools/HydrusImporter/ptr-core/include/ptr/flatten/MappingEvent.hpp`
- Test: `tests/core/ptr/mappingEvent.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `enum class EventOp : std::uint8_t { Add = 0, Delete = 1 }`; `struct MappingEvent { std::uint32_t hash_id, tag_id; std::uint16_t update_index; std::uint8_t op, pad; }` with `sizeof == 12`; `constexpr std::size_t BUCKET_COUNT = 4096`; `constexpr std::size_t bucketFor( std::uint32_t )`; `bool eventLess( const MappingEvent&, const MappingEvent& )`; `constexpr std::uint16_t MAX_UPDATE_INDEX`.

- [ ] **Step 1: Write the failing test**

Create `tests/core/ptr/mappingEvent.cpp`:

```cpp
//
// Created by kj16609 on 7/30/26.
//

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "ptr/flatten/MappingEvent.hpp"

namespace
{

using namespace idhan::hydrus::ptr;

MappingEvent ev( const std::uint32_t hash_id,
                 const std::uint32_t tag_id,
                 const std::uint16_t update_index,
                 const EventOp op )
{
	return MappingEvent { hash_id, tag_id, update_index, static_cast< std::uint8_t >( op ), 0 };
}

TEST( PTRMappingEvent, IsTwelveBytes )
{
	EXPECT_EQ( sizeof( MappingEvent ), 12u );
}

TEST( PTRMappingEvent, BucketIsStableForOneHash )
{
	// Every event for one hash_id must land in exactly one bucket -- this is the property the
	// whole partitioning scheme rests on.
	const auto bucket = bucketFor( 194'644'713u );
	for ( std::uint32_t tag_id = 0; tag_id < 1000; ++tag_id ) EXPECT_EQ( bucketFor( 194'644'713u ), bucket );
	EXPECT_LT( bucket, BUCKET_COUNT );
}

TEST( PTRMappingEvent, BucketCoversFullRange )
{
	EXPECT_EQ( bucketFor( 0u ), 0u );
	EXPECT_EQ( bucketFor( BUCKET_COUNT - 1 ), BUCKET_COUNT - 1 );
	EXPECT_EQ( bucketFor( BUCKET_COUNT ), 0u );
}

TEST( PTRMappingEvent, SortsByHashThenTagThenIndex )
{
	std::vector< MappingEvent > events {
		ev( 2, 1, 1, EventOp::Add ),
		ev( 1, 2, 1, EventOp::Add ),
		ev( 1, 1, 2, EventOp::Add ),
		ev( 1, 1, 1, EventOp::Add ),
	};

	std::ranges::sort( events, eventLess );

	EXPECT_EQ( events[ 0 ].hash_id, 1u );
	EXPECT_EQ( events[ 0 ].tag_id, 1u );
	EXPECT_EQ( events[ 0 ].update_index, 1u );
	EXPECT_EQ( events[ 1 ].update_index, 2u );
	EXPECT_EQ( events[ 2 ].tag_id, 2u );
	EXPECT_EQ( events[ 3 ].hash_id, 2u );
}

TEST( PTRMappingEvent, DeleteSortsAfterAddInSameUpdateIndex )
{
	std::vector< MappingEvent > events { ev( 1, 1, 5, EventOp::Delete ), ev( 1, 1, 5, EventOp::Add ) };

	std::ranges::sort( events, eventLess );

	EXPECT_EQ( events[ 0 ].op, static_cast< std::uint8_t >( EventOp::Add ) );
	EXPECT_EQ( events[ 1 ].op, static_cast< std::uint8_t >( EventOp::Delete ) );
}

} // namespace
```

- [ ] **Step 2: Run it and verify it fails**

```bash
cmake --build build/debug --target IDHANCoreTests -j$(nproc)
```

Expected: FAIL, `fatal error: ptr/flatten/MappingEvent.hpp: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `tools/HydrusImporter/ptr-core/include/ptr/flatten/MappingEvent.hpp`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

namespace idhan::hydrus::ptr
{

//! What a content update did to one (tag, file) pair. Values are load-bearing: Add must be
//! numerically below Delete so that eventLess orders a same-index delete after its add.
enum class EventOp : std::uint8_t
{
	Add = 0,
	Delete = 1
};

#pragma pack( push, 1 )
//! One add-or-delete of one (tag, file) pair, as spilled to a bucket file during the scan.
//! Kept at 12 bytes because the full PTR corpus produces roughly 3.2 billion of these.
struct MappingEvent
{
	std::uint32_t hash_id;
	std::uint32_t tag_id;
	std::uint16_t update_index;
	std::uint8_t op; //!< EventOp
	std::uint8_t pad; //!< Always 0. Keeps the struct at a round 12 bytes.
};
#pragma pack( pop )

static_assert( sizeof( MappingEvent ) == 12, "MappingEvent is written raw to disk; its size is format" );

//! Number of spill buckets. Purely a memory knob: it sets the working-set size of the in-RAM
//! sort in the collapse stage and has no effect on output layout.
inline constexpr std::size_t BUCKET_COUNT { 4096 };

//! Largest update index representable in MappingEvent::update_index. The scan validates against
//! this and aborts rather than truncating.
inline constexpr std::uint16_t MAX_UPDATE_INDEX { 65535 };

//! Every event for a given hash_id lands in exactly one bucket. That is what makes a bucket
//! self-sufficient for collapsing all of its records.
inline constexpr std::size_t bucketFor( const std::uint32_t hash_id )
{
	return hash_id % BUCKET_COUNT;
}

//! Total order (hash_id, tag_id, update_index, op). Sorting a bucket by this makes every
//! (hash_id, tag_id) chain contiguous and chronological, which is what lets collapseChain
//! work over a plain span.
inline bool eventLess( const MappingEvent& a, const MappingEvent& b )
{
	if ( a.hash_id != b.hash_id ) return a.hash_id < b.hash_id;
	if ( a.tag_id != b.tag_id ) return a.tag_id < b.tag_id;
	if ( a.update_index != b.update_index ) return a.update_index < b.update_index;
	return a.op < b.op;
}

} // namespace idhan::hydrus::ptr
```

- [ ] **Step 4: Run the tests and verify they pass**

```bash
cmake --build build/debug --target IDHANCoreTests -j$(nproc)
./build/debug/bin/IDHANCoreTests --gtest_filter='PTRMappingEvent.*'
```

Expected: 5 tests, all PASS.

- [ ] **Step 5: Commit**

```bash
git add tools/HydrusImporter/ptr-core/include/ptr/flatten/MappingEvent.hpp tests/core/ptr/mappingEvent.cpp
git commit -m "feat: add MappingEvent spill record and bucket routing

12-byte packed event with a total order that places a same-index delete
after its add, matching how content updates are applied today.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 3: `collapseChain`

The heart of the feature, and the reason `PTRCore` exists: a pure function with no IO, exhaustively testable.

**Files:**
- Create: `tools/HydrusImporter/ptr-core/include/ptr/flatten/CollapseChain.hpp`
- Create: `tools/HydrusImporter/ptr-core/src/CollapseChain.cpp`
- Test: `tests/core/ptr/collapseChain.cpp`

**Interfaces:**
- Consumes: `MappingEvent`, `EventOp` from Task 2.
- Produces: `struct CollapsedOp { EventOp op; std::uint16_t update_index; }`; `std::optional< CollapsedOp > collapseChain( std::span< const MappingEvent > events )`.

- [ ] **Step 1: Write the failing test**

Create `tests/core/ptr/collapseChain.cpp`:

```cpp
//
// Created by kj16609 on 7/30/26.
//

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "ptr/flatten/CollapseChain.hpp"

namespace
{

using namespace idhan::hydrus::ptr;

//! Builds one key's chain from a compact spelling: 'A' add, 'D' delete. The nth character is
//! given update index n + 1, so the index a rule selects is unambiguous in the assertions.
std::vector< MappingEvent > chain( const std::string_view spelling )
{
	std::vector< MappingEvent > events {};
	std::uint16_t index { 1 };
	for ( const char c : spelling )
	{
		const auto op = c == 'A' ? EventOp::Add : EventOp::Delete;
		events.push_back( MappingEvent { 7, 9, index, static_cast< std::uint8_t >( op ), 0 } );
		++index;
	}
	std::ranges::sort( events, eventLess );
	return events;
}

TEST( PTRCollapseChain, EmptyChainCollapsesToNothing )
{
	EXPECT_FALSE( collapseChain( {} ).has_value() );
}

TEST( PTRCollapseChain, LoneAddSurvives )
{
	const auto events = chain( "A" );
	const auto result = collapseChain( events );
	ASSERT_TRUE( result.has_value() );
	EXPECT_EQ( result->op, EventOp::Add );
	EXPECT_EQ( result->update_index, 1 );
}

TEST( PTRCollapseChain, LoneDeleteSurvives )
{
	// An orphan delete: PTR petitioned a mapping whose add is not in the retained corpus.
	const auto events = chain( "D" );
	const auto result = collapseChain( events );
	ASSERT_TRUE( result.has_value() );
	EXPECT_EQ( result->op, EventOp::Delete );
	EXPECT_EQ( result->update_index, 1 );
}

TEST( PTRCollapseChain, AddThenDeleteKeepsTheDelete )
{
	const auto events = chain( "AD" );
	const auto result = collapseChain( events );
	ASSERT_TRUE( result.has_value() );
	EXPECT_EQ( result->op, EventOp::Delete );
	EXPECT_EQ( result->update_index, 2 );
}

TEST( PTRCollapseChain, AddDeleteAddKeepsTheOriginalAdd )
{
	// The rule that motivated the feature: a re-add is attributed to the first add, not the last.
	const auto events = chain( "ADA" );
	const auto result = collapseChain( events );
	ASSERT_TRUE( result.has_value() );
	EXPECT_EQ( result->op, EventOp::Add );
	EXPECT_EQ( result->update_index, 1 );
}

TEST( PTRCollapseChain, AddDeleteAddDeleteKeepsTheFinalDelete )
{
	const auto events = chain( "ADAD" );
	const auto result = collapseChain( events );
	ASSERT_TRUE( result.has_value() );
	EXPECT_EQ( result->op, EventOp::Delete );
	EXPECT_EQ( result->update_index, 4 );
}

TEST( PTRCollapseChain, LongAlternatingChainKeepsTheOriginalAdd )
{
	const auto events = chain( "ADADA" );
	const auto result = collapseChain( events );
	ASSERT_TRUE( result.has_value() );
	EXPECT_EQ( result->op, EventOp::Add );
	EXPECT_EQ( result->update_index, 1 );
}

TEST( PTRCollapseChain, RepeatedAddsAreIdempotent )
{
	const auto events = chain( "AA" );
	const auto result = collapseChain( events );
	ASSERT_TRUE( result.has_value() );
	EXPECT_EQ( result->op, EventOp::Add );
	EXPECT_EQ( result->update_index, 1 );
}

TEST( PTRCollapseChain, RepeatedDeletesAreIdempotent )
{
	const auto events = chain( "DD" );
	const auto result = collapseChain( events );
	ASSERT_TRUE( result.has_value() );
	EXPECT_EQ( result->op, EventOp::Delete );
	EXPECT_EQ( result->update_index, 2 );
}

TEST( PTRCollapseChain, DeleteWinsWhenBothShareAnUpdateIndex )
{
	// PTR does put an add and a delete for one key in a single update file. eventLess orders the
	// delete second, so the delete is what survives.
	std::vector< MappingEvent > events {
		MappingEvent { 7, 9, 5, static_cast< std::uint8_t >( EventOp::Delete ), 0 },
		MappingEvent { 7, 9, 5, static_cast< std::uint8_t >( EventOp::Add ), 0 },
	};
	std::ranges::sort( events, eventLess );

	const auto result = collapseChain( events );
	ASSERT_TRUE( result.has_value() );
	EXPECT_EQ( result->op, EventOp::Delete );
	EXPECT_EQ( result->update_index, 5 );
}

TEST( PTRCollapseChain, DeleteThenAddKeepsTheAddAtItsOwnIndex )
{
	const auto events = chain( "DA" );
	const auto result = collapseChain( events );
	ASSERT_TRUE( result.has_value() );
	EXPECT_EQ( result->op, EventOp::Add );
	EXPECT_EQ( result->update_index, 2 );
}

} // namespace
```

- [ ] **Step 2: Run it and verify it fails**

```bash
cmake --build build/debug --target IDHANCoreTests -j$(nproc)
```

Expected: FAIL, `fatal error: ptr/flatten/CollapseChain.hpp: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `tools/HydrusImporter/ptr-core/include/ptr/flatten/CollapseChain.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "ptr/flatten/MappingEvent.hpp"

namespace idhan::hydrus::ptr
{

//! The single operation a whole add/delete chain reduces to.
struct CollapsedOp
{
	EventOp op;
	std::uint16_t update_index;
};

//! Reduces one key's history to at most one operation.
//!
//! A chain ending in a delete keeps that delete, at its own index. A chain ending in an add
//! keeps the *first* add in the chain, preserving original attribution across any number of
//! delete/re-add cycles.
//!
//! \param events All events for exactly one (hash_id, tag_id), sorted by eventLess.
//! \return The surviving operation, or nullopt if \p events is empty.
std::optional< CollapsedOp > collapseChain( std::span< const MappingEvent > events );

} // namespace idhan::hydrus::ptr
```

- [ ] **Step 4: Write the implementation**

Create `tools/HydrusImporter/ptr-core/src/CollapseChain.cpp`:

```cpp
#include "ptr/flatten/CollapseChain.hpp"

namespace idhan::hydrus::ptr
{

std::optional< CollapsedOp > collapseChain( const std::span< const MappingEvent > events )
{
	if ( events.empty() ) return std::nullopt;

	// The chain's final state is whatever the last event said. Everything before it is
	// superseded -- that is the entire point of flattening.
	const auto& last = events.back();
	if ( static_cast< EventOp >( last.op ) == EventOp::Delete )
		return CollapsedOp { EventOp::Delete, last.update_index };

	// Terminal add: attribute it to the first add, so a mapping that was deleted and re-added
	// reads as having existed since it was originally applied.
	for ( const auto& event : events )
	{
		if ( static_cast< EventOp >( event.op ) == EventOp::Add )
			return CollapsedOp { EventOp::Add, event.update_index };
	}

	// Unreachable: the last event is an add, so the loop above always finds one.
	return std::nullopt;
}

} // namespace idhan::hydrus::ptr
```

- [ ] **Step 5: Run the tests and verify they pass**

```bash
cmake --build build/debug --target IDHANCoreTests -j$(nproc)
./build/debug/bin/IDHANCoreTests --gtest_filter='PTRCollapseChain.*'
```

Expected: 11 tests, all PASS.

- [ ] **Step 6: Commit**

```bash
git add tools/HydrusImporter/ptr-core/include/ptr/flatten/CollapseChain.hpp tools/HydrusImporter/ptr-core/src/CollapseChain.cpp tests/core/ptr/collapseChain.cpp
git commit -m "feat: add collapseChain, the PTR add/delete chain reduction

A chain ending in a delete keeps that delete; a chain ending in an add keeps
the first add, so a deleted-and-re-added mapping is attributed to when it was
originally applied.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 4: Bucket spill writer and reader

Buffered fan-out to 4096 files, and whole-bucket readback. The buffer size is the dominant RAM cost of the scan stage.

**Files:**
- Create: `tools/HydrusImporter/ptr-core/include/ptr/flatten/BucketSpill.hpp`
- Create: `tools/HydrusImporter/ptr-core/src/BucketSpill.cpp`
- Test: `tests/core/ptr/bucketSpill.cpp`

**Interfaces:**
- Consumes: `MappingEvent`, `BUCKET_COUNT`, `bucketFor` from Task 2.
- Produces: `class BucketWriter` with `explicit BucketWriter( std::filesystem::path dir, std::size_t buffer_events = 5461 )`, `void write( const MappingEvent& )`, `void flush()`, `std::uint64_t written() const noexcept`; free functions `std::filesystem::path bucketPath( const std::filesystem::path& dir, std::size_t bucket )` and `std::vector< MappingEvent > readBucket( const std::filesystem::path& path )`.

- [ ] **Step 1: Write the failing test**

Create `tests/core/ptr/bucketSpill.cpp`:

```cpp
//
// Created by kj16609 on 7/30/26.
//

#include <gtest/gtest.h>

#include <filesystem>
#include <unordered_set>

#include "ptr/flatten/BucketSpill.hpp"

namespace
{

using namespace idhan::hydrus::ptr;

//! Scratch directory removed on destruction, so a failing assertion cannot leak spill files.
class BucketSpillTest : public ::testing::Test
{
  protected:

	void SetUp() override
	{
		m_dir = std::filesystem::temp_directory_path()
		      / ( "ptr-bucket-test-" + std::to_string( ::testing::UnitTest::GetInstance()->random_seed() ) );
		std::filesystem::remove_all( m_dir );
		std::filesystem::create_directories( m_dir );
	}

	void TearDown() override { std::filesystem::remove_all( m_dir ); }

	std::filesystem::path m_dir;
};

TEST_F( BucketSpillTest, RoundTripsASingleEvent )
{
	{
		BucketWriter writer { m_dir };
		writer.write( MappingEvent { 5, 11, 3, static_cast< std::uint8_t >( EventOp::Add ), 0 } );
		writer.flush();
		EXPECT_EQ( writer.written(), 1u );
	}

	const auto events = readBucket( bucketPath( m_dir, bucketFor( 5 ) ) );
	ASSERT_EQ( events.size(), 1u );
	EXPECT_EQ( events[ 0 ].hash_id, 5u );
	EXPECT_EQ( events[ 0 ].tag_id, 11u );
	EXPECT_EQ( events[ 0 ].update_index, 3u );
	EXPECT_EQ( events[ 0 ].op, static_cast< std::uint8_t >( EventOp::Add ) );
}

TEST_F( BucketSpillTest, EveryEventForOneHashLandsInOneBucket )
{
	constexpr std::uint32_t HASH_ID { 194'644'713 };
	constexpr std::uint32_t TAG_COUNT { 500 };

	{
		BucketWriter writer { m_dir };
		for ( std::uint32_t tag_id = 0; tag_id < TAG_COUNT; ++tag_id )
			writer.write( MappingEvent { HASH_ID, tag_id, 1, static_cast< std::uint8_t >( EventOp::Add ), 0 } );
		writer.flush();
	}

	const auto events = readBucket( bucketPath( m_dir, bucketFor( HASH_ID ) ) );
	EXPECT_EQ( events.size(), TAG_COUNT );

	std::uint64_t total_elsewhere { 0 };
	for ( std::size_t bucket = 0; bucket < BUCKET_COUNT; ++bucket )
	{
		if ( bucket == bucketFor( HASH_ID ) ) continue;
		total_elsewhere += readBucket( bucketPath( m_dir, bucket ) ).size();
	}
	EXPECT_EQ( total_elsewhere, 0u );
}

TEST_F( BucketSpillTest, SpillsCorrectlyAcrossBufferBoundaries )
{
	// A tiny buffer forces many mid-stream flushes; the readback must still be exact and ordered.
	constexpr std::uint32_t COUNT { 1000 };
	{
		BucketWriter writer { m_dir, 4 };
		for ( std::uint32_t i = 0; i < COUNT; ++i )
			writer.write( MappingEvent { 0, i, 1, static_cast< std::uint8_t >( EventOp::Add ), 0 } );
		writer.flush();
		EXPECT_EQ( writer.written(), COUNT );
	}

	const auto events = readBucket( bucketPath( m_dir, 0 ) );
	ASSERT_EQ( events.size(), COUNT );
	for ( std::uint32_t i = 0; i < COUNT; ++i ) EXPECT_EQ( events[ i ].tag_id, i );
}

TEST_F( BucketSpillTest, MissingBucketReadsAsEmpty )
{
	EXPECT_TRUE( readBucket( m_dir / "does-not-exist.bucket" ).empty() );
}

TEST_F( BucketSpillTest, DestructorFlushesUnwrittenEvents )
{
	{
		BucketWriter writer { m_dir };
		writer.write( MappingEvent { 1, 1, 1, static_cast< std::uint8_t >( EventOp::Delete ), 0 } );
	}

	EXPECT_EQ( readBucket( bucketPath( m_dir, bucketFor( 1 ) ) ).size(), 1u );
}

} // namespace
```

- [ ] **Step 2: Run it and verify it fails**

```bash
cmake --build build/debug --target IDHANCoreTests -j$(nproc)
```

Expected: FAIL, `fatal error: ptr/flatten/BucketSpill.hpp: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `tools/HydrusImporter/ptr-core/include/ptr/flatten/BucketSpill.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <vector>

#include "ptr/flatten/MappingEvent.hpp"

namespace idhan::hydrus::ptr
{

//! Path of one bucket's spill file inside \p dir.
std::filesystem::path bucketPath( const std::filesystem::path& dir, std::size_t bucket );

//! Reads a whole bucket into memory. A bucket is sized to be comfortably RAM-resident.
//! A file that does not exist reads as empty -- a bucket no event routed to is legitimate.
std::vector< MappingEvent > readBucket( const std::filesystem::path& path );

//! Fans MappingEvents out to BUCKET_COUNT append-only files, buffering each separately.
//!
//! The buffers are the dominant memory cost of the scan stage: BUCKET_COUNT * buffer_events *
//! sizeof(MappingEvent). The default of 5461 events per bucket is roughly 64 KB each, about
//! 256 MB in total. Lower it if that is too much.
class BucketWriter
{
  public:

	static constexpr std::size_t DEFAULT_BUFFER_EVENTS { 5461 };

	explicit BucketWriter( std::filesystem::path dir, std::size_t buffer_events = DEFAULT_BUFFER_EVENTS );

	BucketWriter( const BucketWriter& ) = delete;
	BucketWriter& operator=( const BucketWriter& ) = delete;
	BucketWriter( BucketWriter&& ) = delete;
	BucketWriter& operator=( BucketWriter&& ) = delete;

	//! Flushes every buffer. Errors are logged, not thrown -- a destructor must not throw.
	~BucketWriter();

	void write( const MappingEvent& event );

	//! Writes out every buffer and fsyncs nothing; call before reading any bucket back.
	void flush();

	std::uint64_t written() const noexcept { return m_written; }

  private:

	struct FileCloser
	{
		void operator()( std::FILE* file ) const noexcept;
	};

	using FilePtr = std::unique_ptr< std::FILE, FileCloser >;

	void flushBucket( std::size_t bucket );

	std::filesystem::path m_dir;
	std::size_t m_buffer_events;
	std::vector< FilePtr > m_files;
	std::vector< std::vector< MappingEvent > > m_buffers;
	std::uint64_t m_written { 0 };
};

} // namespace idhan::hydrus::ptr
```

- [ ] **Step 4: Write the implementation**

Create `tools/HydrusImporter/ptr-core/src/BucketSpill.cpp`:

```cpp
#include "ptr/flatten/BucketSpill.hpp"

#include <spdlog/spdlog.h>

#include <format>
#include <stdexcept>

namespace idhan::hydrus::ptr
{

void BucketWriter::FileCloser::operator()( std::FILE* const file ) const noexcept
{
	if ( file != nullptr ) std::fclose( file );
}

std::filesystem::path bucketPath( const std::filesystem::path& dir, const std::size_t bucket )
{
	return dir / std::format( "{:04}.bucket", bucket );
}

std::vector< MappingEvent > readBucket( const std::filesystem::path& path )
{
	std::error_code ec;
	const auto size = std::filesystem::file_size( path, ec );
	if ( ec ) return {};

	if ( size % sizeof( MappingEvent ) != 0 )
		throw std::runtime_error(
			std::format( "Bucket {} is {} bytes, not a whole number of events", path.string(), size ) );

	std::vector< MappingEvent > events( size / sizeof( MappingEvent ) );
	if ( events.empty() ) return events;

	std::FILE* const file = std::fopen( path.c_str(), "rb" );
	if ( file == nullptr ) throw std::runtime_error( std::format( "Failed to open bucket {}", path.string() ) );

	const auto read = std::fread( events.data(), sizeof( MappingEvent ), events.size(), file );
	std::fclose( file );

	if ( read != events.size() )
		throw std::runtime_error(
			std::format( "Short read on bucket {}: got {} of {} events", path.string(), read, events.size() ) );

	return events;
}

BucketWriter::BucketWriter( std::filesystem::path dir, const std::size_t buffer_events ) :
  m_dir( std::move( dir ) ),
  m_buffer_events( buffer_events == 0 ? 1 : buffer_events )
{
	std::filesystem::create_directories( m_dir );

	m_files.resize( BUCKET_COUNT );
	m_buffers.resize( BUCKET_COUNT );
	for ( auto& buffer : m_buffers ) buffer.reserve( m_buffer_events );
}

BucketWriter::~BucketWriter()
{
	try
	{
		flush();
	}
	catch ( const std::exception& e )
	{
		spdlog::error( "BucketWriter failed to flush during destruction: {}", e.what() );
	}
}

void BucketWriter::write( const MappingEvent& event )
{
	const auto bucket = bucketFor( event.hash_id );
	m_buffers[ bucket ].push_back( event );
	++m_written;

	if ( m_buffers[ bucket ].size() >= m_buffer_events ) flushBucket( bucket );
}

void BucketWriter::flush()
{
	for ( std::size_t bucket = 0; bucket < BUCKET_COUNT; ++bucket ) flushBucket( bucket );
}

void BucketWriter::flushBucket( const std::size_t bucket )
{
	auto& buffer = m_buffers[ bucket ];
	if ( buffer.empty() ) return;

	// Opened lazily: a corpus never touches every bucket evenly, and 4096 simultaneously open
	// descriptors is close enough to the default rlimit to be worth avoiding until needed.
	if ( m_files[ bucket ] == nullptr )
	{
		const auto path = bucketPath( m_dir, bucket );
		FilePtr file { std::fopen( path.c_str(), "ab" ) };
		if ( file == nullptr ) throw std::runtime_error( std::format( "Failed to open bucket {}", path.string() ) );
		m_files[ bucket ] = std::move( file );
	}

	const auto wrote = std::fwrite( buffer.data(), sizeof( MappingEvent ), buffer.size(), m_files[ bucket ].get() );
	if ( wrote != buffer.size() )
		throw std::runtime_error(
			std::format( "Short write on bucket {}: {} of {} events", bucket, wrote, buffer.size() ) );

	buffer.clear();
}

} // namespace idhan::hydrus::ptr
```

- [ ] **Step 5: Run the tests and verify they pass**

```bash
cmake --build build/debug --target IDHANCoreTests -j$(nproc)
./build/debug/bin/IDHANCoreTests --gtest_filter='BucketSpillTest.*'
```

Expected: 5 tests, all PASS.

- [ ] **Step 6: Commit**

```bash
git add tools/HydrusImporter/ptr-core/include/ptr/flatten/BucketSpill.hpp tools/HydrusImporter/ptr-core/src/BucketSpill.cpp tests/core/ptr/bucketSpill.cpp
git commit -m "feat: add bucketed spill writer and reader for mapping events

Fans events out to 4096 append-only files with per-bucket buffering, so the
scan stage streams roughly 38 GB to disk at a bounded memory cost.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

**Remaining tasks (5-13) continue in a second file:** `docs/superpowers/plans/2026-07-30-ptr-flattener-part2.md`
