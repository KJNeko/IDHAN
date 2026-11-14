#include "similarRecords.hpp"

#include "IDHANDB.hpp"
#include "idhan/db/DBTransaction.hpp"

namespace idhan::db
{
runtime::Task< std::optional< SimilarityProbe > > similarityProbe(
	DBTransaction& transaction,
	const RecordID record_id )
{
	DBBindings bindings;
	bindings.bind( record_id );
	const auto rows {
		co_await transaction
			.query(
				//language=postgresql
				R"SQL(
					SELECT phash, duplicate_king(record_id) AS king_id
					FROM image_metadata
					WHERE record_id = $1
					  AND phash IS NOT NULL
				)SQL",
				std::move( bindings ) )
			.rows< std::bitset< 64 >, RecordID >()
	};
	if ( rows.empty() ) co_return std::nullopt;

	const auto [ phash, king_id ] { rows.row( 0 ) };
	co_return SimilarityProbe { phash, king_id };
}

runtime::Task< DBRows< RecordID, RecordID > > alternativeKingPairs( DBTransaction& transaction, const RecordID king_id )
{
	DBBindings bindings;
	bindings.bind( king_id );
	co_return co_await transaction
		.query(
			//language=postgresql
			R"SQL(
				SELECT lesser_record_id, greater_record_id
				FROM alternative_records
				WHERE lesser_record_id = $1
				   OR greater_record_id = $1
			)SQL",
			std::move( bindings ) )
		.rows< RecordID, RecordID >();
}

runtime::Task< DBRows< RecordID, RecordID > > unrelatedKingPairs( DBTransaction& transaction, const RecordID king_id )
{
	DBBindings bindings;
	bindings.bind( king_id );
	co_return co_await transaction
		.query(
			//language=postgresql
			R"SQL(
				SELECT lesser_record_id, greater_record_id
				FROM unrelated_records
				WHERE lesser_record_id = $1
				   OR greater_record_id = $1
			)SQL",
			std::move( bindings ) )
		.rows< RecordID, RecordID >();
}

runtime::Task< DBRows< RecordID, Integer, RecordID > > similarityCandidates(
	DBTransaction& transaction,
	const std::bitset< 64 > phash,
	const RecordID record_id,
	const Integer distance )
{
	DBBindings bindings;
	bindings.bind( phash );
	bindings.bind( record_id );
	bindings.bind( distance );
	co_return co_await transaction
		.query(
			//language=postgresql
			R"SQL(
				SELECT record_id,
				       (phash <~> $1::bit(64))::integer AS distance,
				       duplicate_king(record_id) AS king_id
				FROM image_metadata
				WHERE record_id <> $2
				  AND (phash <~> $1::bit(64)) <= $3
				ORDER BY distance, record_id
			)SQL",
			std::move( bindings ) )
		.rows< RecordID, Integer, RecordID >();
}
} // namespace idhan::db
