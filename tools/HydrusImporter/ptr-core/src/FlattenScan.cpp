#include "ptr/flatten/FlattenScan.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <format>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <variant>

#include "ptr/flatten/BucketSpill.hpp"
#include "ptr/flatten/DefinitionStore.hpp"

namespace idhan::hydrus::ptr
{

unsigned defaultScanThreadCount()
{
	return std::max( 1u, std::thread::hardware_concurrency() );
}

namespace
{

//! One update file to process: its hash (== filename) and the update it belongs to.
struct ScanWorkItem
{
	std::string hash_hex;
	std::uint16_t update_index;
};

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
                       const ScanCallbacks& callbacks,
                       const unsigned thread_count )
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

	std::vector< ScanWorkItem > items;
	for ( const auto& update : updates )
		for ( const auto& hash_hex : update.hashes )
			items.push_back( { hash_hex, static_cast< std::uint16_t >( update.index ) } );

	const std::size_t total_files { items.size() };

	DefinitionWriter definitions { work_dir };
	BucketWriter buckets { work_dir };

	// Everything below is shared mutable state; every access to it (including the callbacks) is
	// made through this one lock. Only the decompress-and-parse step, the CPU-bound part, runs
	// unlocked -- that is the whole point of using more than one thread here.
	std::mutex state_mutex;
	std::size_t next_item { 0 };
	std::size_t done { 0 };

	const auto worker = [ & ]
	{
		for ( ;; )
		{
			std::size_t index {};
			{
				std::lock_guard< std::mutex > lock { state_mutex };

				if ( callbacks.cancelled && callbacks.cancelled() ) result.cancelled = true;
				if ( result.cancelled || next_item >= total_files ) return;

				index = next_item++;
			}

			const auto& item = items[ index ];
			const auto path = ptr_dir / ( item.hash_hex + ".ptrupdate" );

			const bool missing = !std::filesystem::exists( path );

			std::optional< ParsedUpdate > parsed;
			std::string parse_error;
			if ( !missing )
			{
				try
				{
					parsed = parseUpdateFile( path );
				}
				catch ( const std::exception& e )
				{
					parse_error = e.what();
				}
			}

			std::lock_guard< std::mutex > lock { state_mutex };

			++done;
			if ( callbacks.progress )
				callbacks.progress( done, total_files, std::format( "Scanning update {}", item.update_index ) );

			if ( missing )
			{
				spdlog::warn( "Update file missing, skipping: {}", path.string() );
				++result.skipped_files;
			}
			else if ( !parse_error.empty() )
			{
				// One unreadable file must not end a multi-hour scan.
				spdlog::error( "Failed to scan {}: {}", item.hash_hex, parse_error );
				++result.skipped_files;
			}
			else if ( const auto* const defs = std::get_if< DefinitionsUpdate >( &*parsed ) )
			{
				for ( const auto& [ hash_id, hex ] : defs->hash_ids_to_hashes )
					definitions.writeHash( static_cast< std::uint32_t >( hash_id ), hex );

				for ( const auto& [ tag_id, tag ] : defs->tag_ids_to_tags )
					definitions.writeTag( static_cast< std::uint32_t >( tag_id ), tag );
			}
			else if ( const auto* const content = std::get_if< ContentUpdate >( &*parsed ) )
			{
				const auto before = buckets.written();

				spillMappings( buckets, content->mappings_add, item.update_index, EventOp::Add );
				spillMappings( buckets, content->mappings_delete, item.update_index, EventOp::Delete );

				result.events_written += buckets.written() - before;

				appendRelations( result.parents, content->tag_parents_add, item.update_index, EventOp::Add );
				appendRelations( result.parents, content->tag_parents_delete, item.update_index, EventOp::Delete );
				appendRelations( result.siblings, content->tag_siblings_add, item.update_index, EventOp::Add );
				appendRelations( result.siblings, content->tag_siblings_delete, item.update_index, EventOp::Delete );
			}

			if ( callbacks.statsUpdated ) callbacks.statsUpdated( result.events_written, result.skipped_files );
		}
	};

	// Never spawn more workers than there is work; harmless but pointless for a small corpus.
	const auto requested = thread_count != 0 ? thread_count : defaultScanThreadCount();
	const auto workers = static_cast< unsigned >( std::min< std::size_t >( requested, total_files ) );
	{
		std::vector< std::jthread > pool;
		pool.reserve( workers );
		for ( unsigned t = 0; t < workers; ++t ) pool.emplace_back( worker );
		// pool's destructor joins every thread here before the function continues.
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
