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

//! Worker threads collapseBuckets uses when \p thread_count is 0 (the default): every reported
//! hardware thread. Every bucket is self-sufficient (MappingEvent.hpp), so reading, sorting, and
//! walking chains for different buckets is independent work.
unsigned defaultCollapseThreadCount();

//! Records per output chunk. Compile-time by design, alongside PTRImportWorker::BATCH_SIZE.
//!
//! This bounds the importer too: it holds one chunk's records and their RecordIDs at a time.
//! At the default the full corpus produces roughly 975 chunks in place of 26,324 update files.
inline constexpr std::size_t MAX_RECORDS_PER_CHUNK { 200'000 };

//! Running collapse-stage counters, passed whole to the progress hook.
//!
//! A struct rather than six positional std::uint64_t parameters: every one has the same type, so a
//! transposed pair at a call site would compile silently and misreport for the whole run.
struct CollapseProgressStats
{
	std::uint64_t records_flattened { 0 };
	std::uint64_t chains_collapsed { 0 };
	std::uint64_t terminal_deletes { 0 };
	std::uint64_t terminal_delete_records { 0 };
	std::uint64_t chunks_written { 0 };
	std::uint64_t skipped_missing_definitions { 0 };
};

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
	//! Fired once per non-empty bucket, after it is processed. The chunk count trails the batch
	//! still being filled and any chunk a peer worker is mid-seal on; the call made once the pool
	//! has joined reports the settled totals.
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

	//! Drop terminal deletes instead of writing them into chunks.
	//!
	//! A chain ending in a delete collapses to that delete alone, never to a paired add, so at
	//! import it is a removeTags for a mapping this import never made. Against a database whose
	//! tags came only from the PTR that is always a no-op, and dropping them costs nothing while
	//! shrinking every chunk. They are still counted either way; only whether they are written
	//! changes.
	bool discard_terminal_deletes { false };

	//! 0 picks defaultCollapseThreadCount().
	unsigned thread_count { 0 };
};

//! Collapses every bucket in \p work_dir into record-major chunks in \p out_dir.
//!
//! Each bucket is read whole, sorted, and scanned once: sorting makes every (hash_id, tag_id)
//! chain contiguous and chronological, so collapsing is a local decision over a span. Because a
//! bucket holds every event for each of its records, a record's output is final when its span ends.
//!
//! options.thread_count worker threads read, sort, and walk chains for different buckets
//! concurrently. Survivors go into one shared batch, but
//! sealing a full batch -- building its string table and deflating tens of megabytes, seconds of
//! CPU repeated hundreds of times over a corpus -- happens with no lock held, so several workers
//! can be sealing at once and none of them blocks the others. All that is serialised is appending
//! records and firing the callbacks.
//!
//! Buckets do not complete in index order, so which chunk a given record lands in is not
//! deterministic across runs. Every record's final tag set, and every total, is unaffected.
//!
//! A chunk spans bucket boundaries. A bucket holds far fewer records than the cap, so confining a
//! chunk to one bucket would make options.max_records_per_chunk unreachable. This is safe because
//! every event for a record lives in exactly one bucket.
//!
//! Records whose hash has no definition are dropped: there is no way to address them. Tags with no
//! definition are dropped from their record and counted.
//!
//! \param usage Marked with every tag id that reaches a chunk, so the caller can tell which of the
//!        corpus's definitions were never written anywhere.
//!
//! \throws Anything a worker threw -- a full disk, a truncated bucket, a zlib failure -- rethrown
//!         here once the pool has joined, so a failure mid-run is reportable rather than fatal to
//!         the process.
CollapseResult collapseBuckets( const std::filesystem::path& work_dir,
                                const std::filesystem::path& out_dir,
                                const DefinitionReader& definitions,
	TagUsageSet& usage,
	const CollapseCallbacks& callbacks,
	CollapseOptions options = {} );

} // namespace idhan::hydrus::ptr
