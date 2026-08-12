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
	std::uint64_t terminal_deletes { 0 }; //!< Terminal deletes found so far (collapse stage).
	std::uint64_t terminal_delete_records { 0 }; //!< Records carrying one so far (collapse stage).
	std::uint64_t chunks_written { 0 }; //!< Chunks closed so far (collapse stage).
	std::uint64_t skipped_files { 0 }; //!< Update files skipped so far (scan stage).
	std::uint64_t skipped_missing_definitions { 0 }; //!< Tag defs missing so far (collapse stage).

	//! Whether terminal deletes were dropped rather than written, so a host can label the counter
	//! above as kept or discarded without having to know what it asked for.
	bool discard_terminal_deletes { false };

	//! Tag definition accounting. Unlike every other counter these cannot be known until the
	//! relations file has been written -- a tag is only unused once nothing more can use it -- so
	//! they stay zero for the whole run and land in the final update. tags_counted says which it
	//! is, so a host can show "pending" rather than a misleading zero.
	bool tags_counted { false };
	std::uint64_t defined_tags { 0 };
	std::uint64_t used_tags { 0 };
};

} // namespace idhan::hydrus::ptr
