#include "ptr/flatten/FlattenCollapse.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <utility>

#include "ptr/flatten/BucketSpill.hpp"
#include "ptr/flatten/ChunkFormat.hpp"
#include "ptr/flatten/CollapseChain.hpp"

namespace idhan::hydrus::ptr
{

namespace
{

//! Owns the chunk currently being filled and rolls over to a new one at the cap. Kept separate
//! from the bucket loop so a chunk can span buckets.
class ChunkSink
{
  public:

	ChunkSink( std::filesystem::path out_dir, const std::size_t cap, const TagLookup& lookup ) :
	  m_out_dir( std::move( out_dir ) ),
	  m_cap( cap == 0 ? 1 : cap ),
	  m_lookup( lookup )
	{}

	ChunkSink( const ChunkSink& ) = delete;
	ChunkSink& operator=( const ChunkSink& ) = delete;
	ChunkSink( ChunkSink&& ) = delete;
	ChunkSink& operator=( ChunkSink&& ) = delete;

	~ChunkSink() = default;

	void add( const std::array< std::byte, SHA256_BYTES >& sha256,
	          std::vector< std::uint32_t > adds,
	          std::vector< std::uint32_t > dels )
	{
		if ( m_writer == nullptr )
		{
			m_current_name = std::format( "chunk-{:05}.idhanptr", m_chunk_index );
			m_writer = std::make_unique< ChunkWriter >( m_out_dir / m_current_name );
		}

		m_writer->addRecord( sha256, std::move( adds ), std::move( dels ) );

		if ( m_writer->recordCount() >= m_cap ) close();
	}

	//! Writes the open chunk, if any, and records it in the entry list.
	void close()
	{
		if ( m_writer == nullptr ) return;

		const auto records = m_writer->recordCount();
		const auto stats = m_writer->finish( m_lookup );

		m_entries.push_back( ChunkEntry { m_current_name, records, stats.mappings } );
		m_missing_definitions += stats.missing_definitions;

		m_writer.reset();
		++m_chunk_index;
	}

	std::vector< ChunkEntry >& entries() noexcept { return m_entries; }

	std::uint64_t missingDefinitions() const noexcept { return m_missing_definitions; }

  private:

	std::filesystem::path m_out_dir;
	std::size_t m_cap;
	const TagLookup& m_lookup;

	std::unique_ptr< ChunkWriter > m_writer {};
	std::string m_current_name {};
	std::size_t m_chunk_index { 0 };
	std::vector< ChunkEntry > m_entries {};
	std::uint64_t m_missing_definitions { 0 };
};

} // namespace

CollapseResult collapseBuckets( const std::filesystem::path& work_dir,
                                const std::filesystem::path& out_dir,
                                const DefinitionReader& definitions,
                                const std::size_t max_records_per_chunk,
                                const CollapseCallbacks& callbacks )
{
	CollapseResult result {};

	std::filesystem::create_directories( out_dir );

	const TagLookup lookup = [ &definitions ]( const std::uint32_t tag_id ) { return definitions.tag( tag_id ); };

	ChunkSink sink { out_dir, max_records_per_chunk, lookup };

	// Counts every record handed to the sink, independent of chunk boundaries -- what the live
	// stats panel calls "records flattened".
	std::uint64_t records_flattened { 0 };

	for ( std::size_t bucket = 0; bucket < BUCKET_COUNT; ++bucket )
	{
		if ( callbacks.cancelled && callbacks.cancelled() )
		{
			result.cancelled = true;
			break;
		}

		if ( callbacks.progress )
			callbacks.progress( bucket + 1, BUCKET_COUNT, std::format( "Collapsing bucket {}", bucket ) );

		auto events = readBucket( bucketPath( work_dir, bucket ) );
		if ( events.empty() ) continue;

		result.stats.events_scanned += events.size();

		std::ranges::sort( events, eventLess );

		// Equal hash_id values are contiguous after the sort, and inside a record so are equal
		// tag_id values, so one linear walk yields whole records without any lookaside structure.
		std::size_t record_start { 0 };
		while ( record_start < events.size() )
		{
			const auto hash_id = events[ record_start ].hash_id;

			std::size_t record_end { record_start };
			while ( record_end < events.size() && events[ record_end ].hash_id == hash_id ) ++record_end;

			const auto sha = definitions.hash( hash_id );
			if ( !sha.has_value() )
			{
				spdlog::warn(
					"No hash definition for hash_id={}, dropping its {} events", hash_id, record_end - record_start );
				record_start = record_end;
				continue;
			}

			std::vector< std::uint32_t > adds;
			std::vector< std::uint32_t > dels;

			std::size_t chain_start { record_start };
			while ( chain_start < record_end )
			{
				const auto tag_id = events[ chain_start ].tag_id;

				std::size_t chain_end { chain_start };
				while ( chain_end < record_end && events[ chain_end ].tag_id == tag_id ) ++chain_end;

				const std::span< const MappingEvent > chain { events.data() + chain_start, chain_end - chain_start };
				if ( const auto collapsed = collapseChain( chain ); collapsed.has_value() )
				{
					if ( collapsed->op == EventOp::Add )
					{
						adds.push_back( tag_id );
					}
					else
					{
						dels.push_back( tag_id );
						++result.stats.terminal_deletes;
					}
					++result.stats.mappings_after_collapse;
				}

				chain_start = chain_end;
			}

			std::array< std::byte, SHA256_BYTES > sha_bytes {};
			std::ranges::copy( *sha, sha_bytes.begin() );

			sink.add( sha_bytes, std::move( adds ), std::move( dels ) );
			++records_flattened;

			record_start = record_end;
		}

		if ( callbacks.statsUpdated )
			callbacks.statsUpdated(
				records_flattened,
				result.stats.events_scanned - result.stats.mappings_after_collapse,
				result.stats.terminal_deletes,
				sink.entries().size(),
				sink.missingDefinitions() );
	}

	sink.close();

	result.chunks = std::move( sink.entries() );
	result.stats.skipped_missing_definitions = sink.missingDefinitions();
	result.stats.events_collapsed = result.stats.events_scanned - result.stats.mappings_after_collapse;

	// sink.close() may have just finished the last, still-open chunk -- report it so the final
	// live snapshot matches the manifest rather than permanently undercounting by one.
	if ( callbacks.statsUpdated )
		callbacks.statsUpdated(
			records_flattened,
			result.stats.events_collapsed,
			result.stats.terminal_deletes,
			result.chunks.size(),
			result.stats.skipped_missing_definitions );

	spdlog::info(
		"Collapse complete: {} events scanned, {} mappings survived ({} collapsed away), {} chunks",
		result.stats.events_scanned,
		result.stats.mappings_after_collapse,
		result.stats.events_collapsed,
		result.chunks.size() );

	return result;
}

} // namespace idhan::hydrus::ptr
