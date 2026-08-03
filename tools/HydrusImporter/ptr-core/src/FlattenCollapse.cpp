#include "ptr/flatten/FlattenCollapse.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <format>
#include <mutex>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "ptr/flatten/BucketSpill.hpp"
#include "ptr/flatten/ChunkFormat.hpp"
#include "ptr/flatten/CollapseChain.hpp"
#include "ptr/flatten/ParallelFor.hpp"

namespace idhan::hydrus::ptr
{

unsigned defaultCollapseThreadCount()
{
	return defaultWorkerCount();
}

namespace
{

//! One record surviving a bucket's collapse, on its way to the chunk sink.
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

//! Collects collapsed records from every worker and writes them out in chunks of \p cap.
//!
//! Shared by all workers and thread-safe. The lock covers only appending to the pending batch and
//! recording a finished chunk; sealing a batch -- building its string table, serialising tens of
//! megabytes and deflating the lot, which is seconds of CPU and happens hundreds of times over a
//! full corpus -- runs with the lock released, so several workers can be sealing at once.
//!
//! Staging in one shared batch rather than one per worker is what keeps chunks full: per-worker
//! batches would leave every worker holding a part-filled chunk at the end, and would multiply the
//! records held in memory by the worker count for no gain.
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

	//! Appends \p records, sealing a chunk whenever the batch reaches the cap. The calling worker
	//! is the one that seals, so the cost lands on whoever filled the batch rather than on a
	//! dedicated thread that could fall behind.
	void add( std::vector< CollapsedRecord > records )
	{
		std::unique_lock< std::mutex > lock { m_mutex };

		for ( auto& record : records )
		{
			m_pending.push_back( std::move( record ) );
			if ( m_pending.size() < m_cap ) continue;

			auto batch = takeBatch();
			lock.unlock();
			seal( std::move( batch ) );
			lock.lock();
		}
	}

	//! Writes whatever is left over. Call once, after every worker has finished.
	void close()
	{
		std::unique_lock< std::mutex > lock { m_mutex };
		if ( m_pending.empty() ) return;

		auto batch = takeBatch();
		lock.unlock();
		seal( std::move( batch ) );
	}

	//! Chunks sealed and tag definitions missed so far. Safe to call while workers are adding.
	std::pair< std::uint64_t, std::uint64_t > totals() const
	{
		std::lock_guard< std::mutex > lock { m_mutex };
		return { m_entries.size(), m_missing_definitions };
	}

	//! Only safe once every worker has finished and close() has run.
	std::vector< ChunkEntry >& entries() noexcept { return m_entries; }

  private:

	//! One chunk's worth of records, claimed by the caller along with the name it will be written
	//! under. \pre The caller holds m_mutex.
	struct Batch
	{
		std::string name;
		std::vector< CollapsedRecord > records;
	};

	Batch takeBatch()
	{
		Batch batch { std::format( "chunk-{:05}.idhanptr", m_next_index++ ), std::move( m_pending ) };
		m_pending = {};
		m_pending.reserve( m_cap );
		return batch;
	}

	//! \pre The caller holds no lock. Nothing else can reach \p batch.
	void seal( Batch batch )
	{
		ChunkWriter writer { m_out_dir / batch.name };
		for ( auto& record : batch.records )
			writer.addRecord( record.sha256, std::move( record.adds ), std::move( record.dels ) );

		const auto records = writer.recordCount();
		const auto stats = writer.finish( m_lookup );

		std::lock_guard< std::mutex > lock { m_mutex };
		m_entries.push_back( ChunkEntry { std::move( batch.name ), records, stats.mappings } );
		m_missing_definitions += stats.missing_definitions;
	}

	std::filesystem::path m_out_dir;
	std::size_t m_cap;
	const TagLookup& m_lookup;

	mutable std::mutex m_mutex {};
	std::vector< CollapsedRecord > m_pending {};
	std::size_t m_next_index { 0 };
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

	// Pure accumulation, bumped once per bucket, so an atomic costs nothing measurable here.
	// records_flattened counts every record handed to the sink, independent of chunk boundaries --
	// what the live stats panel calls "records flattened".
	std::atomic< std::uint64_t > records_flattened { 0 };
	std::atomic< std::uint64_t > events_scanned { 0 };
	std::atomic< std::uint64_t > mappings_after_collapse { 0 };
	std::atomic< std::uint64_t > terminal_deletes { 0 };
	std::atomic< std::size_t > buckets_done { 0 };

	// Guards the host callbacks and nothing else. Reading, sorting and chain-walking a bucket, and
	// building and deflating a chunk, all happen outside it.
	std::mutex callback_mutex;

	const auto body = [ & ]( const std::size_t bucket ) -> bool
	{
		{
			std::lock_guard< std::mutex > lock { callback_mutex };
			if ( callbacks.cancelled && callbacks.cancelled() ) return false;
		}

		auto collapsed = collapseOneBucket( work_dir, bucket, definitions );

		// A bucket with events but zero surviving records (every record's hash undefined) still
		// contributed events_scanned, so the gate is "had events", not "produced records" --
		// otherwise those events would silently vanish from the live and final counters.
		const bool had_events = collapsed.events_scanned > 0;

		if ( had_events )
		{
			events_scanned.fetch_add( collapsed.events_scanned, std::memory_order_relaxed );
			mappings_after_collapse.fetch_add( collapsed.mappings_after_collapse, std::memory_order_relaxed );
			terminal_deletes.fetch_add( collapsed.terminal_deletes, std::memory_order_relaxed );
			records_flattened.fetch_add( collapsed.records.size(), std::memory_order_relaxed );

			sink.add( std::move( collapsed.records ) );
		}

		// Buckets complete out of order, so progress has to count completions. Reporting the index
		// of whichever bucket just finished would make the bar jump backwards.
		const auto done = buckets_done.fetch_add( 1, std::memory_order_relaxed ) + 1;

		std::lock_guard< std::mutex > lock { callback_mutex };

		if ( callbacks.progress )
			callbacks.progress( done, BUCKET_COUNT, std::format( "Collapsing bucket {}", bucket ) );

		// The chunk total trails by the batch still being filled, and by whatever a peer worker is
		// mid-way through sealing. The final callback below reports the settled values.
		if ( had_events && callbacks.statsUpdated )
		{
			const auto [ chunks, missing ] = sink.totals();

			callbacks.statsUpdated(
				records_flattened.load( std::memory_order_relaxed ),
				events_scanned.load( std::memory_order_relaxed )
					- mappings_after_collapse.load( std::memory_order_relaxed ),
				terminal_deletes.load( std::memory_order_relaxed ),
				chunks,
				missing );
		}

		return true;
	};

	result.cancelled = !parallelIndexed( BUCKET_COUNT, thread_count, body );

	sink.close();

	result.stats.skipped_missing_definitions = sink.totals().second;
	result.chunks = std::move( sink.entries() );

	// Chunks are sealed concurrently, so they finish in no particular order. Sorting by name keeps
	// the manifest listing stable and readable; which records a chunk holds still depends on the
	// order buckets completed in.
	std::ranges::sort( result.chunks, []( const auto& a, const auto& b ) { return a.file < b.file; } );

	result.stats.events_scanned = events_scanned.load( std::memory_order_relaxed );
	result.stats.mappings_after_collapse = mappings_after_collapse.load( std::memory_order_relaxed );
	result.stats.terminal_deletes = terminal_deletes.load( std::memory_order_relaxed );
	result.stats.events_collapsed = result.stats.events_scanned - result.stats.mappings_after_collapse;

	// close() above just sealed the last, part-filled chunk -- report it so the final live snapshot
	// matches the manifest rather than permanently undercounting by one.
	if ( callbacks.statsUpdated )
		callbacks.statsUpdated(
			records_flattened.load( std::memory_order_relaxed ),
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
