#include "db/drogonArrayBind.hpp"
#include "phash.hpp"

namespace idhan::metadata
{

drogon::Task< std::int64_t > countMissingPhashes( DbClientPtr db )
{
	const auto rows { co_await db->execSqlCoro(
		"SELECT count(*) FROM image_metadata JOIN file_info USING (record_id) "
		"WHERE phash IS NULL AND cluster_id IS NOT NULL AND cluster_delete_time IS NULL" ) };
	co_return rows[ 0 ][ 0 ].as< std::int64_t >();
}

drogon::Task< std::vector< RecordID > > findMissingPhashes( DbClientPtr db )
{
	const auto rows { co_await db->execSqlCoro(
		"SELECT record_id FROM image_metadata JOIN file_info USING (record_id) "
		"WHERE phash IS NULL AND cluster_id IS NOT NULL AND cluster_delete_time IS NULL "
		"ORDER BY record_id" ) };
	std::vector< RecordID > records {};
	records.reserve( rows.size() );
	for ( const auto& row : rows ) records.emplace_back( row[ "record_id" ].as< RecordID >() );
	co_return records;
}

drogon::Task< std::vector< RecordID > > findMissingPhashes( DbClientPtr db, std::vector< RecordID > candidates )
{
	const auto rows { co_await db->execSqlCoro(
		"SELECT record_id FROM image_metadata JOIN file_info USING (record_id) "
		"WHERE phash IS NULL AND cluster_id IS NOT NULL AND cluster_delete_time IS NULL "
		"AND record_id = ANY($1) ORDER BY record_id",
		std::move( candidates ) ) };
	std::vector< RecordID > records {};
	records.reserve( rows.size() );
	for ( const auto& row : rows ) records.emplace_back( row[ "record_id" ].as< RecordID >() );
	co_return records;
}

} // namespace idhan::metadata
