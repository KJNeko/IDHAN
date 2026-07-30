#pragma once

#include <cstdint>

namespace idhan::hydrus::ptr
{

//! Running counters surfaced during a flatten pass, for progress display. Distinct from the
//! persisted FlattenStats: this exists purely to drive a live UI and is never written to disk.
//!
//! Scan-stage fields (events_scanned, skipped_files) freeze once the scan finishes; collapse-stage
//! fields fill in afterwards. They never overwrite each other, so nothing regresses mid-run.
struct FlattenLiveStats
{
	std::uint64_t events_scanned { 0 }; //!< Mapping events written to buckets so far (scan stage).
	std::uint64_t records_flattened { 0 }; //!< Records handed into a chunk so far (collapse stage).
	std::uint64_t chains_collapsed { 0 }; //!< Events removed by collapsing so far (collapse stage).
	std::uint64_t terminal_deletes { 0 }; //!< Terminal deletes kept so far (collapse stage).
	std::uint64_t chunks_written { 0 }; //!< Chunks closed so far (collapse stage).
	std::uint64_t skipped_files { 0 }; //!< Update files skipped so far (scan stage).
	std::uint64_t skipped_missing_definitions { 0 }; //!< Tag defs missing so far (collapse stage).
};

} // namespace idhan::hydrus::ptr
