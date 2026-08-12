#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "ptr/flatten/ChunkFormat.hpp"

namespace idhan::hydrus::ptr
{

inline constexpr const char* MANIFEST_FILENAME { "compact_manifest.json" };

//! One chunk as listed in the manifest.
struct ChunkEntry
{
	std::string file {};
	std::uint64_t records { 0 };
	std::uint64_t mappings { 0 };
};

//! What a whole flatten run produced. Counters are 64-bit: events_scanned alone exceeds 3e9.
struct FlattenStats
{
	std::uint64_t events_scanned { 0 };
	//! Operations actually written to chunks. With terminal deletes discarded this excludes them,
	//! so the total always agrees with the chunks on disk and events_collapsed absorbs the rest.
	std::uint64_t mappings_after_collapse { 0 };
	std::uint64_t events_collapsed { 0 }; //!< events_scanned minus operations emitted
	std::uint64_t terminal_deletes { 0 }; //!< chains whose last event was a delete, kept or not
	std::uint64_t terminal_delete_records { 0 }; //!< records carrying at least one of them
	std::uint64_t skipped_files { 0 }; //!< update files that failed to parse
	std::uint64_t skipped_missing_definitions { 0 };
	std::uint64_t defined_tags { 0 }; //!< tag ids the corpus defined
	//! Tag ids written to a chunk or the relations file. defined_tags minus this is what was
	//! disregarded as unused: defined by the PTR but never created in IDHAN.
	std::uint64_t used_tags { 0 };
};

//! The index of a compacted directory. Its presence is what marks the directory as compacted,
//! which is why the flattener writes it last: a cancelled run leaves no manifest and therefore
//! cannot be half-imported.
struct CompactManifest
{
	std::uint32_t format_version { CHUNK_FORMAT_VERSION };
	std::int32_t first_update_index { 0 };
	std::int32_t last_update_index { 0 };
	std::uint64_t max_records_per_chunk { 0 };
	//! Whether the run dropped terminal deletes instead of writing them. Records in these chunks
	//! then carry adds only, which the Import tab surfaces so a delete-free import is explainable.
	bool discard_terminal_deletes { false };
	std::vector< ChunkEntry > chunks {};
	std::string relations_file {};
	FlattenStats stats {};
};

void writeManifest( const std::filesystem::path& dir, const CompactManifest& manifest );

//! \throws std::runtime_error if the manifest is absent or unparseable.
CompactManifest readManifest( const std::filesystem::path& dir );

//! \return true if \p dir holds a readable manifest of a supported format version.
bool isCompactedDirectory( const std::filesystem::path& dir );

} // namespace idhan::hydrus::ptr
