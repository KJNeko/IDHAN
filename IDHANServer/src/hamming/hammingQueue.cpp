#include <drogon/drogon.h>

#include <algorithm>
#include <atomic>
#include <span>
#include <vector>

#include "db/drogonArrayBind.hpp"
#include "hamming.hpp"
#include "logging/log.hpp"
#include "threading/SingleRunGuard.hpp"

namespace idhan::hamming
{

static std::atomic< bool > sweep_running { false };

static drogon::Task< void > processQueueBatch( DbClientPtr db, std::vector< RecordID > candidates )
{
	const auto transaction { co_await db->newTransactionCoro() };
	// Serialize sweeps with rulings so a sweep cannot reinsert distances deleted by a ruling.
	co_await transaction->execSqlCoro( "SELECT pg_advisory_xact_lock(16609, 1)" );
	const auto claimed_rows { co_await transaction->execSqlCoro(
		"DELETE FROM hamming_distance_queue WHERE record_id IN ("
		"  SELECT record_id FROM hamming_distance_queue WHERE record_id = ANY($1)"
		"  ORDER BY record_id FOR UPDATE SKIP LOCKED)"
		"RETURNING record_id",
		std::move( candidates ) ) };

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
		"), candidates AS MATERIALIZED ("
		"  SELECT DISTINCT ON (left_id, right_id) left_id, right_id, distance FROM ("
		"    SELECT LEAST(probes.record_id, other.record_id) AS left_id,"
		"           GREATEST(probes.record_id, other.record_id) AS right_id,"
		"           (probes.phash <~> other.phash)::smallint AS distance"
		"    FROM probes JOIN image_metadata other"
		"      ON other.phash IS NOT NULL AND other.record_id <> probes.record_id"
		"    WHERE (probes.phash <~> other.phash) <= $2::integer) computed"
		"  ORDER BY left_id, right_id"
		"), kings AS MATERIALIZED ("
		// Resolve the recursive duplicate view once for the batch, rather than once per pair.
		"  SELECT record_id, root_id FROM flattened_duplicates"
		"), resolved AS ("
		"  SELECT candidates.*, COALESCE(left_king.root_id, left_id) AS left_root,"
		"         COALESCE(right_king.root_id, right_id) AS right_root"
		"  FROM candidates"
		"  LEFT JOIN kings left_king ON left_king.record_id = left_id"
		"  LEFT JOIN kings right_king ON right_king.record_id = right_id"
		"), pairs AS ("
		"  SELECT left_id, right_id, distance FROM resolved"
		"  WHERE left_root <> right_root"
		"    AND NOT EXISTS (SELECT 1 FROM alternative_records alternative"
		"      WHERE alternative.lesser_record_id = LEAST(left_root, right_root)"
		"        AND alternative.greater_record_id = GREATEST(left_root, right_root))"
		"    AND NOT EXISTS (SELECT 1 FROM unrelated_records unrelated"
		"      WHERE unrelated.lesser_record_id = LEAST(left_root, right_root)"
		"        AND unrelated.greater_record_id = GREATEST(left_root, right_root))"
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

	log::info(
		"Computed hamming distances for {} record(s), storing {} pair(s), {} record(s) left to process",
		claimed_count,
		result[ 0 ][ "stored" ].as< std::int64_t >(),
		result[ 0 ][ "remaining" ].as< std::int64_t >() );

	co_return;
}

static drogon::Task< std::vector< RecordID > > findQueuedDistances( DbClientPtr db )
{
	const auto rows { co_await db->execSqlCoro( "SELECT record_id FROM hamming_distance_queue ORDER BY record_id" ) };
	std::vector< RecordID > records {};
	records.reserve( rows.size() );
	for ( const auto& row : rows ) records.emplace_back( row[ "record_id" ].as< RecordID >() );
	co_return records;
}

drogon::Task< void > runQueueSweep( DbClientPtr db )
{
	SingleRunGuard guard { sweep_running };
	if ( !guard.acquired() ) co_return;

	const auto records { co_await findQueuedDistances( db ) };
	const std::span< const RecordID > pending { records };
	for ( std::size_t offset { 0 }; offset < pending.size(); )
	{
		const auto batch {
			pending.subspan( offset, std::min< std::size_t >( QUEUE_BATCH_SIZE, pending.size() - offset ) )
		};
		std::vector< RecordID > candidates { batch.begin(), batch.end() };
		co_await processQueueBatch( db, std::move( candidates ) );
		offset += batch.size();
	}
}

void startQueueSweeper()
{
	drogon::app().getLoop()->runEvery(
		QUEUE_INTERVAL,
		[]()
		{
			drogon::async_run(
				[]() -> drogon::Task< void >
				{
					try
					{
						co_await runQueueSweep( drogon::app().getDbClient() );
					}
					catch ( const std::exception& e )
					{
						log::warn( "Hamming distance sweep failed: {}", e.what() );
					}

					co_return;
				} );
		} );
}

} // namespace idhan::hamming
