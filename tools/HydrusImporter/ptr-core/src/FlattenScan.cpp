#include "ptr/flatten/FlattenScan.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <format>
#include <stdexcept>
#include <utility>
#include <variant>

#include "ptr/flatten/BucketSpill.hpp"
#include "ptr/flatten/DefinitionStore.hpp"

namespace idhan::hydrus::ptr
{

namespace
{

void appendRelations( std::vector< RelationEvent >& out,
                      const std::vector< std::pair< int, int > >& pairs,
                      const std::uint16_t update_index,
                      const EventOp op )
{
	for ( const auto& [ a, b ] : pairs )
	{
		out.push_back( RelationEvent { static_cast< std::uint32_t >( a ),
			                           static_cast< std::uint32_t >( b ),
			                           update_index,
			                           static_cast< std::uint8_t >( op ),
			                           0 } );
	}
}

void spillMappings( BucketWriter& buckets,
                    const std::vector< ContentUpdateMapping >& mappings,
                    const std::uint16_t update_index,
                    const EventOp op )
{
	for ( const auto& mapping : mappings )
	{
		for ( const auto hash_id : mapping.hash_ids )
		{
			buckets.write( MappingEvent { static_cast< std::uint32_t >( hash_id ),
				                          static_cast< std::uint32_t >( mapping.tag_id ),
				                          update_index,
				                          static_cast< std::uint8_t >( op ),
				                          0 } );
		}
	}
}

} // namespace

ScanResult scanCorpus( const std::filesystem::path& ptr_dir,
                       const MetadataUpdate& metadata,
                       const std::filesystem::path& work_dir,
                       const ScanCallbacks& callbacks )
{
	ScanResult result {};

	// Definitions must be written before the content that references them, so the corpus is walked
	// in ascending update index no matter what order the metadata happens to list.
	auto updates = metadata.updates;
	std::ranges::sort( updates, []( const auto& a, const auto& b ) { return a.index < b.index; } );

	std::filesystem::create_directories( work_dir );

	if ( updates.empty() )
	{
		spdlog::warn( "Scan asked to process an empty metadata list" );
		return result;
	}

	for ( const auto& update : updates )
	{
		if ( update.index < 0 || update.index > static_cast< int >( MAX_UPDATE_INDEX ) )
			throw std::runtime_error(
				std::format(
					"Update index {} is outside the range MappingEvent can represent (0..{})",
					update.index,
					MAX_UPDATE_INDEX ) );
	}

	result.first_update_index = updates.front().index;
	result.last_update_index = updates.back().index;

	std::size_t total_files { 0 };
	for ( const auto& update : updates ) total_files += update.hashes.size();

	DefinitionWriter definitions { work_dir };
	BucketWriter buckets { work_dir };

	std::size_t done { 0 };

	for ( const auto& update : updates )
	{
		const auto update_index = static_cast< std::uint16_t >( update.index );

		for ( const auto& hash_hex : update.hashes )
		{
			if ( callbacks.cancelled && callbacks.cancelled() )
			{
				result.cancelled = true;
				buckets.flush();
				result.rejected_hashes = definitions.rejectedHashes();
				return result;
			}

			++done;

			const auto path = ptr_dir / ( hash_hex + ".ptrupdate" );

			if ( callbacks.progress )
				callbacks.progress( done, total_files, std::format( "Scanning update {}", update.index ) );

			if ( !std::filesystem::exists( path ) )
			{
				spdlog::warn( "Update file missing, skipping: {}", path.string() );
				++result.skipped_files;
			}
			else
			{
				try
				{
					auto parsed = parseUpdateFile( path );

					if ( const auto* const defs = std::get_if< DefinitionsUpdate >( &parsed ) )
					{
						for ( const auto& [ hash_id, hex ] : defs->hash_ids_to_hashes )
							definitions.writeHash( static_cast< std::uint32_t >( hash_id ), hex );

						for ( const auto& [ tag_id, tag ] : defs->tag_ids_to_tags )
							definitions.writeTag( static_cast< std::uint32_t >( tag_id ), tag );
					}
					else if ( const auto* const content = std::get_if< ContentUpdate >( &parsed ) )
					{
						const auto before = buckets.written();

						spillMappings( buckets, content->mappings_add, update_index, EventOp::Add );
						spillMappings( buckets, content->mappings_delete, update_index, EventOp::Delete );

						result.events_written += buckets.written() - before;

						appendRelations( result.parents, content->tag_parents_add, update_index, EventOp::Add );
						appendRelations( result.parents, content->tag_parents_delete, update_index, EventOp::Delete );
						appendRelations( result.siblings, content->tag_siblings_add, update_index, EventOp::Add );
						appendRelations( result.siblings, content->tag_siblings_delete, update_index, EventOp::Delete );
					}
				}
				catch ( const std::exception& e )
				{
					// One unreadable file must not end a multi-hour scan.
					spdlog::error( "Failed to scan {}: {}", hash_hex, e.what() );
					++result.skipped_files;
				}
			}

			if ( callbacks.statsUpdated ) callbacks.statsUpdated( result.events_written, result.skipped_files );
		}
	}

	buckets.flush();
	result.rejected_hashes = definitions.rejectedHashes();

	spdlog::info(
		"Scan complete: {} events, {} files skipped, {} hashes rejected, {} parent and {} sibling events",
		result.events_written,
		result.skipped_files,
		result.rejected_hashes,
		result.parents.size(),
		result.siblings.size() );

	return result;
}

} // namespace idhan::hydrus::ptr
