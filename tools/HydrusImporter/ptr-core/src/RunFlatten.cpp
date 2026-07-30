#include "ptr/flatten/RunFlatten.hpp"

#include <json/json.h>

#include <spdlog/spdlog.h>

#include <format>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <variant>

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
                           const std::uint64_t required_free_bytes )
{
	FlattenOutcome outcome {};

	const auto work_dir = out_dir / WORK_SUBDIRECTORY;

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

		announce( callbacks, "Scanning update files" );
		ScanCallbacks scan_callbacks {};
		scan_callbacks.cancelled = callbacks.cancelled;
		scan_callbacks.progress = callbacks.progress;

		const auto scan = scanCorpus( ptr_dir, metadata, work_dir, scan_callbacks );

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

			collapse = collapseBuckets( work_dir, out_dir, definitions, max_records_per_chunk, collapse_callbacks );

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
			relation_stats = writeRelationsFile( out_dir / RELATIONS_FILENAME, parents, siblings, lookup );
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
