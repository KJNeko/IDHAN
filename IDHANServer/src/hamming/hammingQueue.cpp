#include <drogon/drogon.h>

#include <atomic>
#include <vector>

#include "db/drogonArrayBind.hpp"
#include "hamming.hpp"
#include "logging/log.hpp"

namespace idhan::hamming
{

static std::atomic< bool > sweep_running { false };

drogon::Task< void > processQueueBatch( DbClientPtr db )
{
	const auto transaction { co_await db->newTransactionCoro() };
	const auto claimed_rows { co_await transaction->execSqlCoro(
		"DELETE FROM hamming_distance_queue WHERE record_id IN ("
		"  SELECT record_id FROM hamming_distance_queue ORDER BY record_id"
		"  LIMIT $1::integer FOR UPDATE SKIP LOCKED)"
		"RETURNING record_id",
		QUEUE_BATCH_SIZE ) };

	if ( claimed_rows.empty() ) co_return;

	std::vector< RecordID > claimed {};
	claimed.reserve( claimed_rows.size() );
	for ( const auto& row : claimed_rows ) claimed.emplace_back( row[ "record_id" ].as< RecordID >() );
	const auto claimed_count { claimed.size() };

	co_await transaction->execSqlCoro(
		"DELETE FROM hamming_distance WHERE left_id = ANY($1) OR right_id = ANY($1)",
		std::vector< RecordID > { claimed } );

	const auto result { co_await transaction->execSqlCoro(
		"WITH probes AS ("
		"  SELECT record_id, phash FROM image_metadata"
		"  WHERE record_id = ANY($1) AND phash IS NOT NULL"
		"), pairs AS ("
		"  SELECT DISTINCT ON (left_id, right_id) left_id, right_id, distance FROM ("
		"    SELECT LEAST(probes.record_id, other.record_id) AS left_id,"
		"           GREATEST(probes.record_id, other.record_id) AS right_id,"
		"           bit_count(probes.phash # other.phash)::smallint AS distance"
		"    FROM probes JOIN image_metadata other"
		"      ON other.phash IS NOT NULL AND other.record_id <> probes.record_id"
		"    WHERE bit_count(probes.phash # other.phash) <= $2::integer) computed"
		"  ORDER BY left_id, right_id"
		"), stored AS ("
		"  INSERT INTO hamming_distance (left_id, right_id, distance)"
		"  SELECT left_id, right_id, distance FROM pairs"
		"  ON CONFLICT (left_id, right_id) DO UPDATE SET distance = excluded.distance"
		"  RETURNING 1"
		") SELECT (SELECT count(*) FROM stored) AS stored,"
		"  (SELECT count(*) FROM hamming_distance_queue) AS remaining",
		std::move( claimed ),
		MAX_DISTANCE ) };

	if ( result.empty() ) co_return;

	log::debug(
		"Computed hamming distances for {} record(s), storing {} pair(s), {} record(s) left to process",
		claimed_count,
		result[ 0 ][ "stored" ].as< std::int64_t >(),
		result[ 0 ][ "remaining" ].as< std::int64_t >() );

	co_return;
}

void startQueueSweeper()
{
	drogon::app().getLoop()->runEvery(
		QUEUE_INTERVAL,
		[]()
		{
			if ( sweep_running.exchange( true ) ) return;

			drogon::async_run(
				[]() -> drogon::Task< void >
				{
					try
					{
						co_await processQueueBatch( drogon::app().getDbClient() );
					}
					catch ( const std::exception& e )
					{
						log::warn( "Hamming distance sweep failed: {}", e.what() );
					}

					sweep_running.store( false );
					co_return;
				} );
		} );
}

} // namespace idhan::hamming
