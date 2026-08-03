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

//! Worker threads collapseBuckets uses when \p thread_count is 0 (the default): every reported
//! hardware thread. Every bucket is self-sufficient (MappingEvent.hpp), so reading, sorting, and
//! walking chains for different buckets is independent work.
unsigned defaultCollapseThreadCount();

//! Records per output chunk. Compile-time by design, alongside PTRImportWorker::BATCH_SIZE.
//!
//! This bounds the importer too: it holds one chunk's records and their RecordIDs at a time.
//! At the default the full corpus produces roughly 975 chunks in place of 26,324 update files.
inline constexpr std::size_t MAX_RECORDS_PER_CHUNK { 200'000 };

//! Host hooks. All may be empty; collapseBuckets checks before calling.
//!
//! \warning These are called from collapseBuckets's worker threads, not from the calling thread.
//!          They are serialised against each other, so they need no locking of their own and may
//!          share mutable state -- but one slow callback stalls every worker, so keep them to
//!          bumping counters or emitting a queued signal.
struct CollapseCallbacks
{
	//! Polled once per bucket. Return true to stop early.
	std::function< bool() > cancelled {};
	//! (buckets done, buckets total, current status text). Counts completions, not bucket indices:
	//! buckets finish out of order.
	std::function< void( std::size_t, std::size_t, std::string_view ) > progress {};
	//! Fired once per non-empty bucket, after it is processed: (records flattened so far, chains
	//! collapsed so far, terminal deletes kept so far, chunks written so far, tag definitions
	//! missing so far). The chunk count trails the batch still being filled and any chunk a peer
	//! worker is mid-seal on; the call made once the pool has joined reports the settled totals.
	std::function< void( std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t ) >
		statsUpdated {};
};

//! What the collapse produced.
struct CollapseResult
{
	std::vector< ChunkEntry > chunks {};
	FlattenStats stats {};
	bool cancelled { false };
};

//! Collapses every bucket in \p work_dir into record-major chunks in \p out_dir.
//!
//! Each bucket is read whole, sorted, and scanned once: sorting makes every (hash_id, tag_id)
//! chain contiguous and chronological, so collapsing is a local decision over a span. Because a
//! bucket holds every event for each of its records, a record's output is final when its span ends.
//!
//! \p thread_count worker threads read, sort, and walk chains for different buckets concurrently;
//! 0 (the default) picks defaultCollapseThreadCount(). Survivors go into one shared batch, but
//! sealing a full batch -- building its string table and deflating tens of megabytes, seconds of
//! CPU repeated hundreds of times over a corpus -- happens with no lock held, so several workers
//! can be sealing at once and none of them blocks the others. All that is serialised is appending
//! records and firing the callbacks.
//!
//! Buckets do not complete in index order, so which chunk a given record lands in is not
//! deterministic across runs. Every record's final tag set, and every total, is unaffected.
//!
//! A chunk spans bucket boundaries. A bucket holds far fewer records than the cap, so confining a
//! chunk to one bucket would make \p max_records_per_chunk unreachable. This is safe because every
//! event for a record lives in exactly one bucket.
//!
//! Records whose hash has no definition are dropped: there is no way to address them. Tags with no
//! definition are dropped from their record and counted.
//!
//! \throws Anything a worker threw -- a full disk, a truncated bucket, a zlib failure -- rethrown
//!         here once the pool has joined, so a failure mid-run is reportable rather than fatal to
//!         the process.
CollapseResult collapseBuckets( const std::filesystem::path& work_dir,
                                const std::filesystem::path& out_dir,
                                const DefinitionReader& definitions,
                                std::size_t max_records_per_chunk,
                                const CollapseCallbacks& callbacks,
                                unsigned thread_count = 0 );

} // namespace idhan::hydrus::ptr
