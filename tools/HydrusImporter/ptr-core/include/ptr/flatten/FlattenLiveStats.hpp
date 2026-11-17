#pragma once

#include <cstdint>

namespace idhan::hydrus::ptr
{

//! Running counters surfaced during a flatten pass for progress display. Never persisted.
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

	//! Whether terminal deletes were dropped rather than written.
	bool discard_terminal_deletes { false };

	//! Tag definition counters are known only once the relations file is written; until then
	//! they stay zero and tags_counted is false.
	bool tags_counted { false };
	std::uint64_t defined_tags { 0 };
	std::uint64_t used_tags { 0 };
};

} // namespace idhan::hydrus::ptr
