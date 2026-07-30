#include "ptr/flatten/FlattenCollapse.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <format>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <utility>

#include "ptr/flatten/BucketSpill.hpp"
#include "ptr/flatten/ChunkFormat.hpp"
#include "ptr/flatten/CollapseChain.hpp"

namespace idhan::hydrus::ptr
{

unsigned defaultCollapseThreadCount()
{
	return std::max( 1u, std::thread::hardware_concurrency() );
}

namespace
{

//! One record surviving a bucket's collapse, not yet handed to the (shared, locked) chunk sink.
struct CollapsedRecord
{
	std::array< std::byte, SHA256_BYTES > sha256;
	std::vector< std::uint32_t > adds;
	std::vector< std::uint32_t > dels;
};

//! Everything one bucket's collapse produced, computed with no shared state touched.
struct BucketCollapseResult
{
	std::vector< CollapsedRecord > records {};
	std::uint64_t events_scanned { 0 };
	std::uint64_t mappings_after_collapse { 0 };
	std::uint64_t terminal_deletes { 0 };
};

//! Reads, sorts, and walks one bucket's events into whole records. Touches only its arguments --
//! safe to run concurrently for different buckets.
BucketCollapseResult collapseOneBucket( const std::filesystem::path& work_dir,
                                        const std::size_t bucket,
                                        const DefinitionReader& definitions )
{
	BucketCollapseResult out {};

	auto events = readBucket( bucketPath( work_dir, bucket ) );
	if ( events.empty() ) return out;

	out.events_scanned = events.size();

	std::ranges::sort( events, eventLess );

	// Equal hash_id values are contiguous after the sort, and inside a record so are equal tag_id
	// values, so one linear walk yields whole records without any lookaside structure.
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
					++out.terminal_deletes;
				}
				++out.mappings_after_collapse;
			}

			chain_start = chain_end;
		}

		std::array< std::byte, SHA256_BYTES > sha_bytes {};
		std::ranges::copy( *sha, sha_bytes.begin() );

		out.records.push_back( { sha_bytes, std::move( adds ), std::move( dels ) } );

		record_start = record_end;
	}

	return out;
}

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
                                const CollapseCallbacks& callbacks,
                                const unsigned thread_count )
{
	CollapseResult result {};

	std::filesystem::create_directories( out_dir );

	const TagLookup lookup = [ &definitions ]( const std::uint32_t tag_id ) { return definitions.tag( tag_id ); };

	ChunkSink sink { out_dir, max_records_per_chunk, lookup };

	// Counts every record handed to the sink, independent of chunk boundaries -- what the live
	// stats panel calls "records flattened".
	std::uint64_t records_flattened { 0 };

	// Everything below is shared mutable state; every access to it (including the callbacks) is
	// made through this one lock. Reading, sorting, and chain-walking a bucket -- the CPU-bound
	// part -- runs unlocked in collapseOneBucket.
	std::mutex state_mutex;
	std::size_t next_bucket { 0 };

	const auto worker = [ & ]
	{
		for ( ;; )
		{
			std::size_t bucket {};
			{
				std::lock_guard< std::mutex > lock { state_mutex };

				if ( callbacks.cancelled && callbacks.cancelled() ) result.cancelled = true;
				if ( result.cancelled || next_bucket >= BUCKET_COUNT ) return;

				bucket = next_bucket++;
			}

			auto collapsed = collapseOneBucket( work_dir, bucket, definitions );

			std::lock_guard< std::mutex > lock { state_mutex };

			if ( callbacks.progress )
				callbacks.progress( bucket + 1, BUCKET_COUNT, std::format( "Collapsing bucket {}", bucket ) );

			// A bucket with events but zero surviving records (every record's hash undefined) still
			// contributed events_scanned, so the gate is "had events", not "produced records" --
			// otherwise those events would silently vanish from the live and final counters.
			if ( collapsed.events_scanned > 0 )
			{
				result.stats.events_scanned += collapsed.events_scanned;
				result.stats.mappings_after_collapse += collapsed.mappings_after_collapse;
				result.stats.terminal_deletes += collapsed.terminal_deletes;

				for ( auto& record : collapsed.records )
				{
					sink.add( record.sha256, std::move( record.adds ), std::move( record.dels ) );
					++records_flattened;
				}

				if ( callbacks.statsUpdated )
					callbacks.statsUpdated(
						records_flattened,
						result.stats.events_scanned - result.stats.mappings_after_collapse,
						result.stats.terminal_deletes,
						sink.entries().size(),
						sink.missingDefinitions() );
			}
		}
	};

	// Never spawn more workers than there are buckets; harmless but pointless for a tiny corpus.
	const auto requested = thread_count != 0 ? thread_count : defaultCollapseThreadCount();
	const auto workers = static_cast< unsigned >( std::min< std::size_t >( requested, BUCKET_COUNT ) );
	{
		std::vector< std::jthread > pool;
		pool.reserve( workers );
		for ( unsigned t = 0; t < workers; ++t ) pool.emplace_back( worker );
		// pool's destructor joins every thread here before the function continues.
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
