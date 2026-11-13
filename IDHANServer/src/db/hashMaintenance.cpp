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

runtime::Task< hamming::PhashIndex > loadPhashIndex( DBContext& db )
{
	const auto rows {
		co_await db.query( "SELECT record_id, phash FROM image_metadata WHERE phash IS NOT NULL ORDER BY record_id" )
			.rows< RecordID, std::bitset< 64 > >()
	};
	std::vector< RecordID > records;
	std::vector< std::uint64_t > hashes;
	records.reserve( rows.size() );
	hashes.reserve( rows.size() );
	for ( const auto& [ id, hash ] : rows )
	{
		records.push_back( id );
		hashes.push_back( hash.to_ullong() );
	}
	co_return hamming::PhashIndex { std::move( records ), std::move( hashes ) };
}

runtime::Task< std::vector< RecordPhash > > currentPhashes(
	DBTransaction& transaction,
	std::vector< RecordID > records )
{
	DBBindings bindings;
	bindings.bind( std::move( records ) );
	const auto rows {
		co_await transaction
			.query(
				"SELECT requested.record_id, i.phash FROM unnest($1::integer[]) AS requested(record_id) "
				"LEFT JOIN image_metadata i USING (record_id)",
				std::move( bindings ) )
			.rows< RecordID, std::optional< std::bitset< 64 > > >()
	};
	std::vector< RecordPhash > phashes;
	phashes.reserve( rows.size() );
	for ( const auto& [ id, hash ] : rows )
	{
		std::optional< std::uint64_t > value;
		if ( hash.has_value() ) value = hash.value().to_ullong();

		phashes.push_back( { id, value } );
	}
	co_return phashes;
}

runtime::Task< std::int64_t > replaceHammingPairs(
	DBTransaction& transaction,
	std::vector< RecordID > records,
	std::vector< hamming::HammingPair > pairs )
{
	DBBindings remove;
	remove.bind( std::move( records ) );
	co_await transaction
		.query(
			"DELETE FROM hamming_distance WHERE left_id = ANY($1::integer[]) OR right_id = ANY($1::integer[])",
			std::move( remove ) )
		.result();

	if ( pairs.empty() ) co_return 0;

	std::vector< RecordID > left_ids;
	std::vector< RecordID > right_ids;
	std::vector< SmallInt > distances;
	left_ids.reserve( pairs.size() );
	right_ids.reserve( pairs.size() );
	distances.reserve( pairs.size() );
	for ( const auto& pair : pairs )
	{
		left_ids.push_back( pair.left_id );
		right_ids.push_back( pair.right_id );
		distances.push_back( pair.distance );
	}
	DBBindings bindings;
	bindings.bind( std::move( left_ids ) );
	bindings.bind( std::move( right_ids ) );
	bindings.bind( std::move( distances ) );
	const auto rows {
		co_await transaction
			.query(
				"WITH candidates AS (SELECT * FROM unnest($1::integer[], $2::integer[], $3::smallint[]) "
				"AS computed(left_id, right_id, distance)), "
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

runtime::Task< std::optional< RecordID > > missingPhashHighWater( DBContext& db, std::vector< MimeID > phash_mimes )
{
	DBBindings bindings;
	bindings.bind( phash_mimes );
	const auto rows {
		co_await db
			.query(
				"SELECT MAX(i.record_id) FROM image_metadata i JOIN file_info f USING (record_id) "
				"WHERE i.phash IS NULL AND f.cluster_id IS NOT NULL AND f.cluster_delete_time IS NULL "
				"AND f.mime_id = ANY($1::integer[])",
				std::move( bindings ) )
			.rows< std::optional< RecordID > >()
	};
	co_return std::get< 0 >( rows.row( 0 ) );
}

runtime::Task< std::vector< MissingPhashRecord > > missingPhashBatch(
	DBContext& db,
	const std::optional< RecordID > after,
	const RecordID through,
	const std::size_t limit,
	std::vector< MimeID > phash_mimes )
{
	DBBindings bindings;
	bindings.bind( after );
	bindings.bind( through );
	bindings.bind( static_cast< std::int64_t >( limit ) );
	bindings.bind( phash_mimes );
	const auto rows {
		co_await db
			.query(
				"SELECT i.record_id, f.cluster_id, f.mime_id, r.sha256, COALESCE(f.extension, m.best_extension, '') "
				"FROM image_metadata i JOIN file_info f USING (record_id) JOIN records r USING (record_id) "
				"LEFT JOIN mime m ON m.mime_id = f.mime_id "
				"WHERE i.phash IS NULL AND f.cluster_id IS NOT NULL AND f.cluster_delete_time IS NULL "
				"AND f.mime_id = ANY($4::integer[]) "
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
