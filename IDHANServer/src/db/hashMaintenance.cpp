#include "hashMaintenance.hpp"

#include <bitset>

#include "idhan/db/DBContext.hpp"

namespace idhan::db
{

runtime::Task< std::optional< RecordID > > queuedHammingHighWater( DBContext& db )
{
	const auto rows {
		co_await db.query( "SELECT MAX(record_id) FROM hamming_distance_queue" ).rows< std::optional< RecordID > >()
	};
	co_return std::get< 0 >( rows.row( 0 ) );
}

runtime::Task< std::vector< RecordID > > claimHammingBatch(
	DBTransaction& transaction,
	const std::optional< RecordID > after,
	const RecordID through,
	const std::size_t limit )
{
	DBBindings bindings;
	bindings.bind( after );
	bindings.bind( through );
	bindings.bind( static_cast< std::int64_t >( limit ) );
	const auto rows {
		co_await transaction
			.query(
				"DELETE FROM hamming_distance_queue WHERE record_id IN ("
				"SELECT record_id FROM hamming_distance_queue "
				"WHERE ($1::integer IS NULL OR record_id > $1) AND record_id <= $2 "
				"ORDER BY record_id LIMIT $3 FOR UPDATE SKIP LOCKED) RETURNING record_id",
				std::move( bindings ) )
			.rows< RecordID >()
	};
	std::vector< RecordID > claimed;
	claimed.reserve( rows.size() );
	for ( const auto& [ id ] : rows ) claimed.push_back( id );
	co_return claimed;
}

runtime::Task< std::int64_t > rebuildHammingPairs( DBTransaction& transaction, std::vector< RecordID > records )
{
	DBBindings remove;
	remove.bind( records );
	co_await transaction
		.query(
			"DELETE FROM hamming_distance WHERE left_id = ANY($1::integer[]) OR right_id = ANY($1::integer[])",
			std::move( remove ) )
		.result();
	DBBindings bindings;
	bindings.bind( records );
	const auto rows {
		co_await transaction
			.query(
				"WITH probes AS (SELECT record_id, phash FROM image_metadata "
				"WHERE record_id = ANY($1::integer[]) AND phash IS NOT NULL), "
				"candidates AS MATERIALIZED (SELECT DISTINCT ON (left_id, right_id) left_id, right_id, distance FROM ("
				"SELECT LEAST(probes.record_id, other.record_id) AS left_id, "
				"GREATEST(probes.record_id, other.record_id) AS right_id, "
				"(probes.phash <~> other.phash)::smallint AS distance "
				"FROM probes JOIN image_metadata other ON other.phash IS NOT NULL AND other.record_id <> probes.record_id "
				"WHERE (probes.phash <~> other.phash) <= 8) computed ORDER BY left_id, right_id), "
				"resolved AS (SELECT candidates.*, COALESCE(l.king_id, left_id) AS left_root, "
				"COALESCE(r.king_id, right_id) AS right_root FROM candidates "
				"LEFT JOIN duplicate_members l ON l.record_id = left_id "
				"LEFT JOIN duplicate_members r ON r.record_id = right_id), "
				"pairs AS (SELECT left_id, right_id, distance FROM resolved WHERE left_root <> right_root "
				"AND NOT EXISTS (SELECT 1 FROM alternative_records a "
				"WHERE a.lesser_record_id = LEAST(left_root, right_root) AND a.greater_record_id = GREATEST(left_root, right_root)) "
				"AND NOT EXISTS (SELECT 1 FROM unrelated_records u "
				"WHERE u.lesser_record_id = LEAST(left_root, right_root) AND u.greater_record_id = GREATEST(left_root, right_root))), "
				"stored AS (INSERT INTO hamming_distance (left_id, right_id, distance) "
				"SELECT left_id, right_id, distance FROM pairs "
				"ON CONFLICT (left_id, right_id) DO UPDATE SET distance = excluded.distance RETURNING 1) "
				"SELECT count(*)::bigint FROM stored",
				std::move( bindings ) )
			.rows< std::int64_t >()
	};
	co_return std::get< 0 >( rows.row( 0 ) );
}

runtime::Task< std::optional< RecordID > > missingPhashHighWater( DBContext& db )
{
	const auto rows {
		co_await db
			.query(
				"SELECT MAX(i.record_id) FROM image_metadata i JOIN file_info f USING (record_id) "
				"WHERE i.phash IS NULL AND f.cluster_id IS NOT NULL AND f.cluster_delete_time IS NULL" )
			.rows< std::optional< RecordID > >()
	};
	co_return std::get< 0 >( rows.row( 0 ) );
}

runtime::Task< std::vector< MissingPhashRecord > > missingPhashBatch(
	DBContext& db,
	const std::optional< RecordID > after,
	const RecordID through,
	const std::size_t limit )
{
	DBBindings bindings;
	bindings.bind( after );
	bindings.bind( through );
	bindings.bind( static_cast< std::int64_t >( limit ) );
	const auto rows {
		co_await db
			.query(
				"SELECT i.record_id, f.cluster_id, f.mime_id, r.sha256, COALESCE(f.extension, m.best_extension, '') "
				"FROM image_metadata i JOIN file_info f USING (record_id) JOIN records r USING (record_id) "
				"LEFT JOIN mime m ON m.mime_id = f.mime_id "
				"WHERE i.phash IS NULL AND f.cluster_id IS NOT NULL AND f.cluster_delete_time IS NULL "
				"AND ($1::integer IS NULL OR i.record_id > $1) AND i.record_id <= $2 ORDER BY i.record_id LIMIT $3",
				std::move( bindings ) )
			.rows< RecordID, ClusterID, std::optional< MimeID >, std::span< const std::byte >, std::string >()
	};
	std::vector< MissingPhashRecord > records;
	records.reserve( rows.size() );
	for ( const auto& [ id, cluster, mime, hash, extension ] : rows )
		records.push_back( { id, cluster, mime, SHA256::fromBuffer( hash ), extension } );
	co_return records;
}

runtime::Task< bool > updateMissingPhash( DBContext& db, const MissingPhashRecord& record, const PerceptualHash& hash )
{
	std::bitset< 64 > bits;
	for ( const auto byte : hash )
		bits = ( bits << 8 ) | std::bitset< 64 > { std::to_integer< std::uint64_t >( byte ) };
	DBBindings bindings;
	bindings.bind( record.record_id );
	bindings.bind( bits );
	bindings.bind( record.cluster_id );
	bindings.bind( record.mime_id );
	const auto rows {
		co_await db
			.query(
				"UPDATE image_metadata i SET phash = $2::bit(64) FROM file_info f "
				"WHERE i.record_id = $1 AND i.phash IS NULL AND f.record_id = i.record_id "
				"AND f.cluster_id = $3 AND f.cluster_delete_time IS NULL AND f.mime_id IS NOT DISTINCT FROM $4::integer "
				"RETURNING i.record_id",
				std::move( bindings ) )
			.rows< RecordID >()
	};
	co_return !rows.empty();
}
} // namespace idhan::db
