#include <algorithm>

#include "db/hashMaintenance.hpp"
#include "PhashIndex.hpp"
#include "hamming.hpp"
#include "idhan/db/DBContext.hpp"
#include "logging/log.hpp"

namespace idhan::hamming
{
runtime::Task< std::optional< QueueBatchResult > > processQueueBatch(
	db::DBContext& db,
	PhashIndex& index,
	const RecordID through,
	const std::optional< RecordID > after )
{
	auto transaction { co_await db.transaction() };
	// Must precede queue/pair row locks, matching all relationship mutation paths.
	co_await transaction
			 .query(
				 //language=postgresql
				 R"SQL(
				SELECT
				    pg_advisory_xact_lock(16609, 1)
			)SQL" )
			 .result();
	const auto claimed { co_await db::claimHammingBatch( transaction, after, through, queueBatchSize ) };
	if ( claimed.empty() )
	{
		co_await transaction.commit();
		co_return std::nullopt;
	}

	const auto phashes { co_await db::currentPhashes( transaction, claimed ) };
	for ( const auto& [ record, phash ] : phashes ) index.assign( record, phash );
	auto pairs { index.pairsWithin( claimed, maxStoredDistance ) };
	const auto stored { co_await db::replaceHammingPairs( transaction, claimed, std::move( pairs ) ) };
	co_await transaction.commit();
	log::info( "Computed hamming distances for {} records, storing {} pairs", claimed.size(), stored );
	co_return QueueBatchResult { std::ranges::max( claimed ), claimed.size() };
}

runtime::Task< std::uint64_t > runQueueSweep( db::DBContext& db, const std::stop_token stop )
{
	const auto through { co_await db::queuedHammingHighWater( db ) };
	if ( !through ) co_return 0;

	auto index { co_await db::loadPhashIndex( db ) };
	std::optional< RecordID > after;
	std::uint64_t processed {};
	while ( !stop.stop_requested() )
	{
		const auto batch { co_await processQueueBatch( db, index, *through, after ) };
		if ( !batch ) break;

		after = batch->last_record_id;
		processed += batch->processed_records;
	}
	co_return processed;
}
} // namespace idhan::hamming
