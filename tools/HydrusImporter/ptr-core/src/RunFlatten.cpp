#include "ptr/flatten/RunFlatten.hpp"

#include <json/json.h>

#include <spdlog/spdlog.h>

#include <format>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "ptr/flatten/DefinitionStore.hpp"
#include "ptr/flatten/FlattenScan.hpp"
#include "ptr/flatten/RelationsFile.hpp"

namespace idhan::hydrus::ptr
{

namespace
{

void announce( const FlattenCallbacks& callbacks, const std::string_view text )
{
	spdlog::info( "Flatten: {}", text );
	if ( callbacks.stage ) callbacks.stage( text );
}

//! Removes the scratch directory, logging rather than throwing: failing to clean up must not
//! turn a successful flatten into a failed one.
void removeWorkDirectory( const std::filesystem::path& work_dir )
{
	std::error_code ec;
	std::filesystem::remove_all( work_dir, ec );
	if ( ec ) spdlog::warn( "Failed to remove the flatten work directory {}: {}", work_dir.string(), ec.message() );
}

//! Moves the finished chunks and the relations file out of the staging directory into \p out_dir.
//!
//! Everything a run produces is written inside the work directory until this call, so a cancelled
//! or failed run leaves the output directory exactly as it found it. Without that, a run abandoned
//! partway through the collapse would strand hundreds of chunk files that no manifest describes and
//! that cannot be told apart from a previous good run's output.
//!
//! Staging is nested inside \p out_dir, so the two are always on one filesystem and every move is a
//! rename rather than a copy.
void publishStagedOutput( const std::filesystem::path& staging_dir,
                          const std::filesystem::path& out_dir,
                          const CompactManifest& manifest )
{
	std::vector< std::filesystem::path > moved;

	const auto move = [ & ]( const std::string& name )
	{
		std::filesystem::rename( staging_dir / name, out_dir / name );
		moved.push_back( out_dir / name );
	};

	try
	{
		for ( const auto& chunk : manifest.chunks ) move( chunk.file );
		if ( !manifest.relations_file.empty() ) move( manifest.relations_file );
	}
	catch ( ... )
	{
		// Put the output directory back the way it was found. A partial chunk set with no manifest
		// is not importable, but it is still wasted disk and a puzzle for whoever looks next.
		std::error_code ec;
		for ( const auto& path : moved ) std::filesystem::remove( path, ec );
		throw;
	}
}

} // namespace

MetadataUpdate loadCorpusMetadata( const std::filesystem::path& dir )
{
	const auto cache_path = dir / "ptr_metadata.json";
	{
		std::ifstream file { cache_path };
		if ( file )
		{
			Json::Value root;
			Json::CharReaderBuilder builder;
			std::string errors;
			if ( Json::parseFromStream( builder, file, &root, &errors ) )
			{
				auto metadata = parseMetadataCacheJson( root );
				spdlog::info(
					"Loaded metadata from {}: {} update indices", cache_path.string(), metadata.updates.size() );
				return metadata;
			}
			spdlog::warn( "Failed to parse {}: {}", cache_path.string(), errors );
		}
	}

	const auto raw_path = dir / "metadata.ptrupdate";
	if ( std::filesystem::exists( raw_path ) )
	{
		try
		{
			auto parsed = parseUpdateFile( raw_path );
			if ( auto* const metadata = std::get_if< MetadataUpdate >( &parsed ) )
			{
				spdlog::info(
					"Loaded metadata from {}: {} update indices", raw_path.string(), metadata->updates.size() );
				return std::move( *metadata );
			}
			spdlog::warn( "{} did not parse as a MetadataUpdate", raw_path.string() );
		}
		catch ( const std::exception& e )
		{
			spdlog::warn( "Failed to parse {}: {}", raw_path.string(), e.what() );
		}
	}

	throw std::runtime_error(
		std::format( "No PTR metadata found in {}. Run the download step first.", dir.string() ) );
}

FlattenOutcome runFlatten( const std::filesystem::path& ptr_dir,
                           const std::filesystem::path& out_dir,
                           const FlattenCallbacks& callbacks,
                           const std::size_t max_records_per_chunk,
                           const std::uint64_t required_free_bytes,
                           const unsigned scan_thread_count,
                           const unsigned collapse_thread_count )
{
	FlattenOutcome outcome {};

	const auto work_dir = out_dir / WORK_SUBDIRECTORY;

	// Chunks and the relations file are built in here and moved out only once the run has actually
	// succeeded, so removing the work directory is all the cleanup any failure path needs.
	const auto staging_dir = work_dir / OUTPUT_STAGING_SUBDIRECTORY;

	try
	{
		std::filesystem::create_directories( out_dir );

		// Fail before spending hours, not after filling the disk mid-spill.
		const auto space = std::filesystem::space( out_dir );
		if ( space.available < required_free_bytes )
		{
			outcome.message = std::format(
				"Not enough free space at {}: {} GiB available, {} GiB required",
				out_dir.string(),
				space.available / ( 1024ULL * 1024 * 1024 ),
				required_free_bytes / ( 1024ULL * 1024 * 1024 ) );
			spdlog::error( "{}", outcome.message );
			return outcome;
		}

		announce( callbacks, "Loading metadata" );
		const auto metadata = loadCorpusMetadata( ptr_dir );

		// Owned here and updated in place by both stages' callbacks below, so scan-stage fields
		// stay put once collapse begins instead of resetting to zero.
		FlattenLiveStats live {};

		announce( callbacks, "Scanning update files" );
		ScanCallbacks scan_callbacks {};
		scan_callbacks.cancelled = callbacks.cancelled;
		scan_callbacks.progress = callbacks.progress;
		scan_callbacks.statsUpdated =
			[ &callbacks, &live ]( const std::uint64_t events_scanned, const std::uint64_t skipped_files )
		{
			live.events_scanned = events_scanned;
			live.skipped_files = skipped_files;
			if ( callbacks.statsUpdated ) callbacks.statsUpdated( live );
		};

		const auto scan = scanCorpus( ptr_dir, metadata, work_dir, scan_callbacks, scan_thread_count );

		if ( scan.cancelled )
		{
			outcome.cancelled = true;
			outcome.message = "Cancelled during scan";
			removeWorkDirectory( work_dir );
			return outcome;
		}

		announce( callbacks, "Collapsing chains" );

		CollapseResult collapse {};
		RelationsFileStats relation_stats {};

		{
			// Scoped so the mmap is released before the work directory is removed.
			const DefinitionReader definitions { work_dir };

			CollapseCallbacks collapse_callbacks {};
			collapse_callbacks.cancelled = callbacks.cancelled;
			collapse_callbacks.progress = callbacks.progress;
			collapse_callbacks.statsUpdated = [ &callbacks, &live ]( const std::uint64_t records_flattened,
			                                                        const std::uint64_t chains_collapsed,
			                                                        const std::uint64_t terminal_deletes,
			                                                        const std::uint64_t chunks_written,
			                                                        const std::uint64_t skipped_missing_definitions )
			{
				live.records_flattened = records_flattened;
				live.chains_collapsed = chains_collapsed;
				live.terminal_deletes = terminal_deletes;
				live.chunks_written = chunks_written;
				live.skipped_missing_definitions = skipped_missing_definitions;
				if ( callbacks.statsUpdated ) callbacks.statsUpdated( live );
			};

			collapse = collapseBuckets(
				work_dir, staging_dir, definitions, max_records_per_chunk, collapse_callbacks, collapse_thread_count );

			if ( collapse.cancelled )
			{
				outcome.cancelled = true;
				outcome.message = "Cancelled during collapse";
				removeWorkDirectory( work_dir );
				return outcome;
			}

			announce( callbacks, "Writing relations" );

			const auto parents = collapseRelations( scan.parents );
			const auto siblings = collapseRelations( scan.siblings );

			const TagLookup lookup = [ &definitions ]( const std::uint32_t tag_id )
			{ return definitions.tag( tag_id ); };
			relation_stats = writeRelationsFile( staging_dir / RELATIONS_FILENAME, parents, siblings, lookup );
		}

		outcome.manifest.format_version = CHUNK_FORMAT_VERSION;
		outcome.manifest.first_update_index = scan.first_update_index;
		outcome.manifest.last_update_index = scan.last_update_index;
		outcome.manifest.max_records_per_chunk = max_records_per_chunk;
		outcome.manifest.relations_file = RELATIONS_FILENAME;
		outcome.manifest.chunks = collapse.chunks;
		outcome.manifest.stats = collapse.stats;
		outcome.manifest.stats.skipped_files = scan.skipped_files;
		outcome.manifest.stats.skipped_missing_definitions += relation_stats.missing_definitions;

		// Written last. Until this exists the directory is not a compacted directory.
		announce( callbacks, "Writing manifest" );
		publishStagedOutput( staging_dir, out_dir, outcome.manifest );
		writeManifest( out_dir, outcome.manifest );

		removeWorkDirectory( work_dir );

		outcome.success = true;
		outcome.message = std::format(
			"Flattened {} events into {} mappings across {} chunks",
			outcome.manifest.stats.events_scanned,
			outcome.manifest.stats.mappings_after_collapse,
			outcome.manifest.chunks.size() );

		spdlog::info( "{}", outcome.message );
	}
	catch ( const std::exception& e )
	{
		outcome.success = false;
		outcome.message = e.what();
		spdlog::error( "Flatten failed: {}", e.what() );
		removeWorkDirectory( work_dir );
	}

	return outcome;
}

} // namespace idhan::hydrus::ptr
