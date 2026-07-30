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
//! walking chains for different buckets is independent work; only handing survivors to the shared
//! chunk writer is serialised.
unsigned defaultCollapseThreadCount();

//! Records per output chunk. Compile-time by design, alongside PTRImportWorker::BATCH_SIZE.
//!
//! This bounds the importer too: it holds one chunk's records and their RecordIDs at a time.
//! At the default the full corpus produces roughly 975 chunks in place of 26,324 update files.
inline constexpr std::size_t MAX_RECORDS_PER_CHUNK { 200'000 };

//! Host hooks. All may be empty; collapseBuckets checks before calling.
struct CollapseCallbacks
{
	//! Polled once per bucket. Return true to stop early.
	std::function< bool() > cancelled {};
	//! (buckets done, buckets total, current status text)
	std::function< void( std::size_t, std::size_t, std::string_view ) > progress {};
	//! Fired once per non-empty bucket, after it is processed: (records flattened so far, chains
	//! collapsed so far, terminal deletes kept so far, chunks written so far, tag definitions
	//! missing so far).
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
//! only handing the survivors to the shared chunk writer is serialised, since that part is cheap
//! next to a bucket's disk read and sort. Buckets no longer necessarily complete in index order, so
//! which chunk a given record lands in is not deterministic across runs -- every record's final
//! tag set is unaffected. 0 (the default) picks defaultCollapseThreadCount().
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
                                const CollapseCallbacks& callbacks,
                                unsigned thread_count = 0 );

} // namespace idhan::hydrus::ptr
