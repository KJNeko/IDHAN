#include <drogon/drogon.h>

#include <atomic>

#include "hamming.hpp"
#include "logging/log.hpp"

namespace idhan::hamming
{

static std::atomic< bool > sweep_running { false };

drogon::Task< void > processQueueBatch( DbClientPtr db )
{
	const auto result { co_await db->execSqlCoro(
		"WITH claimed AS ("
		"  DELETE FROM hamming_distance_queue WHERE record_id IN ("
		"    SELECT record_id FROM hamming_distance_queue ORDER BY record_id"
		"    LIMIT $1::integer FOR UPDATE SKIP LOCKED)"
		"  RETURNING record_id"
		"), probes AS ("
		"  SELECT image_metadata.record_id, image_metadata.phash FROM image_metadata"
		"  JOIN claimed USING (record_id) WHERE image_metadata.phash IS NOT NULL"
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
		") SELECT (SELECT count(*) FROM claimed) AS claimed, (SELECT count(*) FROM stored) AS stored,"
		"  (SELECT count(*) FROM hamming_distance_queue) - (SELECT count(*) FROM claimed) AS remaining",
		QUEUE_BATCH_SIZE,
		MAX_DISTANCE ) };

	if ( result.empty() ) co_return;

	const auto claimed { result[ 0 ][ "claimed" ].as< std::int64_t >() };
	if ( claimed == 0 ) co_return;

	log::debug(
		"Computed hamming distances for {} record(s), storing {} pair(s), {} record(s) left to process",
		claimed,
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
