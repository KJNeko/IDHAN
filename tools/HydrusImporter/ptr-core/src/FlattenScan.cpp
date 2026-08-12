#include "ptr/flatten/FlattenScan.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <format>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

#include "ptr/flatten/BucketSpill.hpp"
#include "ptr/flatten/DefinitionStore.hpp"
#include "ptr/flatten/ParallelFor.hpp"

namespace idhan::hydrus::ptr
{

unsigned defaultScanThreadCount()
{
	return defaultWorkerCount();
}

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

//! \return The number of events written, so the caller never has to diff a shared counter.
std::uint64_t spillMappings( BucketWriter& buckets,
                             const std::vector< ContentUpdateMapping >& mappings,
                             const std::uint16_t update_index,
                             const EventOp op )
{
	std::uint64_t written { 0 };

	for ( const auto& mapping : mappings )
	{
		for ( const auto hash_id : mapping.hash_ids )
		{
			buckets.write( MappingEvent { static_cast< std::uint32_t >( hash_id ),
				                          static_cast< std::uint32_t >( mapping.tag_id ),
				                          update_index,
				                          static_cast< std::uint8_t >( op ),
				                          0 } );
			++written;
		}
	}

	return written;
}


ScanResult scanCorpus( const std::filesystem::path& ptr_dir,
                       const MetadataUpdate& metadata,
                       const std::filesystem::path& work_dir,
                       const ScanCallbacks& callbacks,
                       const unsigned thread_count )
{
	ScanResult result {};

	// Sorted only to pin down the update index range reported in the manifest. It does not order
	// the work: files are dispatched in this order but complete in whatever order the workers
	// finish, and nothing in the scan reads back what another file wrote. The definition store is
	// whole once the pool joins, which is the barrier the collapse stage actually depends on.
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

	// Pure accumulation, never read for a decision, so an atomic is enough. They are bumped once
	// per file rather than once per event, so the shared cache line is cold.
	std::atomic< std::uint64_t > events_written { 0 };
	std::atomic< std::uint64_t > skipped_files { 0 };
	std::atomic< std::size_t > files_done { 0 };

	// Guards the relation vectors and the host callbacks, and nothing else. Everything expensive --
	// inflate, parse, hex decode, definition writes, bucket spilling -- runs outside it, because
	// BucketWriter and DefinitionWriter do their own per-bucket and per-reservation locking.
	std::mutex merge_mutex;

	const auto body = [ & ]( const std::size_t index ) -> bool
	{
		{
			std::lock_guard< std::mutex > lock { merge_mutex };
			if ( callbacks.cancelled && callbacks.cancelled() ) return false;
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

		// Built here and merged under the lock below. The corpus holds roughly 186,000 relation
		// events in total, so one file's share is nothing next to holding the lock while parsing.
		std::vector< RelationEvent > parents;
		std::vector< RelationEvent > siblings;

		if ( missing )
		{
			spdlog::warn( "Update file missing, skipping: {}", path.string() );
			skipped_files.fetch_add( 1, std::memory_order_relaxed );
		}
		else if ( !parse_error.empty() )
		{
			// One unreadable file must not end a multi-hour scan.
			spdlog::error( "Failed to scan {}: {}", item.hash_hex, parse_error );
			skipped_files.fetch_add( 1, std::memory_order_relaxed );
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
			const auto spilled = spillMappings( buckets, content->mappings_add, item.update_index, EventOp::Add )
			                   + spillMappings( buckets, content->mappings_delete, item.update_index, EventOp::Delete );
			events_written.fetch_add( spilled, std::memory_order_relaxed );

			appendRelations( parents, content->tag_parents_add, item.update_index, EventOp::Add );
			appendRelations( parents, content->tag_parents_delete, item.update_index, EventOp::Delete );
			appendRelations( siblings, content->tag_siblings_add, item.update_index, EventOp::Add );
			appendRelations( siblings, content->tag_siblings_delete, item.update_index, EventOp::Delete );
		}

		// Every counter this file touches is already published, so whichever worker takes the lock
		// last reports totals that include all of them -- which is what makes the final callback
		// agree with the returned result.
		const auto done = files_done.fetch_add( 1, std::memory_order_relaxed ) + 1;

		std::lock_guard< std::mutex > lock { merge_mutex };

		result.parents.insert( result.parents.end(), parents.begin(), parents.end() );
		result.siblings.insert( result.siblings.end(), siblings.begin(), siblings.end() );

		if ( callbacks.progress )
			callbacks.progress( done, total_files, std::format( "Scanning update {}", item.update_index ) );

		if ( callbacks.statsUpdated )
			callbacks.statsUpdated(
				events_written.load( std::memory_order_relaxed ), skipped_files.load( std::memory_order_relaxed ) );

		return true;
	};

	result.cancelled = !parallelIndexed( total_files, thread_count, body );

	buckets.flush();

	result.events_written = events_written.load( std::memory_order_relaxed );
	result.skipped_files = skipped_files.load( std::memory_order_relaxed );
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
