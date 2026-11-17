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

//! Scratch subdirectory of the output directory, removed once the chunks are written.
inline constexpr const char* WORK_SUBDIRECTORY { "work" };

//! Staging area inside the work directory, moved into the output directory once the run succeeds.
inline constexpr const char* OUTPUT_STAGING_SUBDIRECTORY { "output" };

//! Default output location: a subdirectory of the PTR files directory.
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

//! How a flatten run should behave. Defaults are production's.
struct FlattenOptions
{
	std::size_t max_records_per_chunk { MAX_RECORDS_PER_CHUNK };
	std::uint64_t required_free_bytes { REQUIRED_FREE_BYTES };
	unsigned scan_thread_count { 0 }; //!< 0 picks defaultScanThreadCount()
	unsigned collapse_thread_count { 0 }; //!< 0 picks defaultCollapseThreadCount()

	//! See CollapseOptions::discard_terminal_deletes. Recorded in the manifest.
	bool discard_terminal_deletes { false };
};

//! Scans, collapses, writes relations, and finally writes the manifest. Output is staged and moved
//! into \p out_dir at the end, so a cancelled or failed run leaves it untouched.
FlattenOutcome runFlatten( const std::filesystem::path& ptr_dir,
                           const std::filesystem::path& out_dir,
                           const FlattenCallbacks& callbacks,
	FlattenOptions options = {} );

} // namespace idhan::hydrus::ptr
