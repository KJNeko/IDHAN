#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

#include "ptr/PTRFileParser.hpp"
#include "ptr/flatten/FlattenCollapse.hpp"
#include "ptr/flatten/FlattenLiveStats.hpp"
#include "ptr/flatten/Manifest.hpp"

namespace idhan::hydrus::ptr
{

//! Scratch subdirectory of the output directory. Holds the buckets and the definition store, and
//! is removed once the chunks are written -- chunks carry their own strings, so nothing in here
//! is needed at import time.
inline constexpr const char* WORK_SUBDIRECTORY { "work" };

//! Where chunks and the relations file are built, inside the work directory. They are moved into
//! the output directory only once the run has succeeded, so a cancelled or failed run leaves the
//! output directory untouched instead of stranding chunks that no manifest describes.
inline constexpr const char* OUTPUT_STAGING_SUBDIRECTORY { "output" };

//! Conventional output location: a subdirectory of the PTR files directory, so a corpus and its
//! compacted form travel together rather than as unrelated sibling folders.
//!
//! Nesting the output inside the source is safe. The scan resolves update files by exact name
//! from the metadata rather than globbing the directory, so it never sees the output, and the
//! Import tab keys on compact_manifest.json, which exists only in the subdirectory.
inline constexpr const char* COMPACT_SUBDIRECTORY { "compact" };

//! Free space required before a flatten will start. The full corpus spills roughly 38 GB of
//! buckets plus 7.6 GB of definitions; 60 GB leaves room for the chunks written alongside.
inline constexpr std::uint64_t REQUIRED_FREE_BYTES { 60ULL * 1024 * 1024 * 1024 };

//! Host hooks. Every member may be empty; runFlatten checks before calling.
struct FlattenCallbacks
{
	//! Polled frequently. Return true to stop early.
	std::function< bool() > cancelled {};
	//! Called once as each stage begins.
	std::function< void( std::string_view ) > stage {};
	//! (done, total, status text) within the current stage.
	std::function< void( std::size_t, std::size_t, std::string_view ) > progress {};
	//! Running counters, merged across scan and collapse. Fired at the same cadence as progress.
	std::function< void( const FlattenLiveStats& ) > statsUpdated {};
};

//! What a flatten run produced.
struct FlattenOutcome
{
	bool success { false };
	bool cancelled { false };
	std::string message {};
	CompactManifest manifest {};
};

//! Loads the corpus metadata, preferring the downloader's ptr_metadata.json and falling back to
//! the raw metadata.ptrupdate.
//! \throws std::runtime_error if neither is present or usable.
MetadataUpdate loadCorpusMetadata( const std::filesystem::path& dir );

//! How a flatten run should behave. Defaults are production's, so a caller overrides only what it
//! actually cares about -- which for everything but discard_terminal_deletes means the tests.
struct FlattenOptions
{
	std::size_t max_records_per_chunk { MAX_RECORDS_PER_CHUNK };
	std::uint64_t required_free_bytes { REQUIRED_FREE_BYTES };
	unsigned scan_thread_count { 0 }; //!< 0 picks defaultScanThreadCount()
	unsigned collapse_thread_count { 0 }; //!< 0 picks defaultCollapseThreadCount()

	//! See CollapseOptions::discard_terminal_deletes. Recorded in the manifest, so an import can
	//! explain why a corpus applies no tag removals.
	bool discard_terminal_deletes { false };
};

//! Scans, collapses, writes relations, and finally writes the manifest.
//!
//! Every file the run produces is built inside the work directory and moved into \p out_dir at the
//! end, with the manifest written last of all: its presence is what marks a directory as compacted.
//! A cancelled or failed run therefore leaves \p out_dir exactly as it found it.
FlattenOutcome runFlatten( const std::filesystem::path& ptr_dir,
                           const std::filesystem::path& out_dir,
                           const FlattenCallbacks& callbacks,
	FlattenOptions options = {} );

} // namespace idhan::hydrus::ptr
