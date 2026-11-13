#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string_view>
#include <vector>

#include "ptr/flatten/DefinitionStore.hpp"
#include "ptr/flatten/Manifest.hpp"
#include "ptr/flatten/TagUsageSet.hpp"

namespace idhan::hydrus::ptr
{

//! Every reported hardware thread, the count CollapseOptions::thread_count = 0 stands for.
//! TODO: unused; parallelIndexed falls back to defaultWorkerCount() directly.
unsigned defaultCollapseThreadCount();

//! Records per output chunk. Also bounds the importer, which holds one chunk at a time.
inline constexpr std::size_t MAX_RECORDS_PER_CHUNK { 200'000 };

//! Running collapse-stage counters, passed whole to the progress hook.
struct CollapseProgressStats
{
	std::uint64_t records_flattened { 0 };
	std::uint64_t chains_collapsed { 0 };
	std::uint64_t terminal_deletes { 0 };
	std::uint64_t terminal_delete_records { 0 };
	std::uint64_t chunks_written { 0 };
	std::uint64_t skipped_missing_definitions { 0 };
};

//! Host hooks. All may be empty.
//!
//! \warning Called from worker threads, serialised against each other. A slow callback stalls
//!          every worker.
struct CollapseCallbacks
{
	//! Polled once per bucket. Return true to stop early.
	std::function< bool() > cancelled {};
	//! (buckets done, buckets total, current status text). Counts completions, not bucket indices:
	//! buckets finish out of order.
	std::function< void( std::size_t, std::size_t, std::string_view ) > progress {};
	//! Fired once per non-empty bucket. Totals settle in the final call, after the pool joins.
	std::function< void( const CollapseProgressStats& ) > statsUpdated {};
};

//! What the collapse produced.
struct CollapseResult
{
	std::vector< ChunkEntry > chunks {};
	FlattenStats stats {};
	bool cancelled { false };
};

//! How a collapse should behave. Defaults are production's.
struct CollapseOptions
{
	std::size_t max_records_per_chunk { MAX_RECORDS_PER_CHUNK };

	//! Drop terminal deletes instead of writing them into chunks. They are counted either way.
	bool discard_terminal_deletes { false };

	//! 0 picks defaultCollapseThreadCount().
	unsigned thread_count { 0 };
};

//! Collapses every bucket in \p work_dir into record-major chunks in \p out_dir.
//!
//! Buckets are processed concurrently, so which chunk a record lands in is not deterministic;
//! every record's final tag set and every total is. Records whose hash has no definition are
//! dropped; tags with no definition are dropped from their record and counted.
//!
//! \param usage Marked with every tag id that reaches a chunk.
//! \throws Anything a worker threw, rethrown once the pool has joined.
CollapseResult collapseBuckets( const std::filesystem::path& work_dir,
                                const std::filesystem::path& out_dir,
                                const DefinitionReader& definitions,
	TagUsageSet& usage,
	const CollapseCallbacks& callbacks,
	CollapseOptions options = {} );

} // namespace idhan::hydrus::ptr
