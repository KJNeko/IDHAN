#include "api/FileRelationshipsAPI.hpp"
#include "api/helpers/helpers.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > FileRelationshipsAPI::getRelationships(
	[[maybe_unused]] drogon::HttpRequestPtr request,
	const RecordID record_id )
{
	const auto db { drogon::app().getDbClient() };

	const auto validation { co_await helpers::validateRecordIds( { record_id }, db ) };
	if ( !validation ) co_return validation.error();

	auto inferior_result { db->execSqlCoro(
		"SELECT inferior.inferior_id FROM duplicate_groups duplicate_group "
		"JOIN duplicate_group_inferiors inferior USING (duplicate_id) "
		"WHERE duplicate_group.king_id = $1 ORDER BY inferior.inferior_id",
		record_id ) };
	auto superior_result { db->execSqlCoro(
		"SELECT duplicate_group.king_id FROM duplicate_group_inferiors inferior "
		"JOIN duplicate_groups duplicate_group USING (duplicate_id) WHERE inferior.inferior_id = $1",
		record_id ) };
	auto alternative_result { db->execSqlCoro(
		"SELECT greater_record_id AS record_id FROM alternative_records WHERE lesser_record_id = $1"
		"  UNION"
		"  SELECT lesser_record_id FROM alternative_records WHERE greater_record_id = $1"
		" ORDER BY record_id",
		record_id ) };

	Json::Value json {};
	json[ "inferior" ] = Json::arrayValue;
	json[ "superior" ] = Json::arrayValue;
	json[ "alternatives" ] = Json::arrayValue;

	for ( const auto& row : co_await inferior_result ) json[ "inferior" ].append( row[ 0 ].as< RecordID >() );

	for ( const auto& row : co_await superior_result ) json[ "superior" ].append( row[ 0 ].as< RecordID >() );

	for ( const auto& row : co_await alternative_result ) json[ "alternatives" ].append( row[ 0 ].as< RecordID >() );

	co_return drogon::HttpResponse::newHttpJsonResponse( json );
}

} // namespace idhan::api
