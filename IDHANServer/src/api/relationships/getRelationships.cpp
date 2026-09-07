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
		"SELECT worse_record_id FROM duplicate_pairs "
		"WHERE better_record_id = $1 ORDER BY worse_record_id",
		record_id ) };
	auto superior_result {
		db->execSqlCoro( "SELECT better_record_id FROM duplicate_pairs WHERE worse_record_id = $1", record_id )
	};
	// Resolve inherited alternatives through the record's king.
	auto alternative_result { db->execSqlCoro(
		"SELECT greater_record_id AS record_id FROM alternative_records"
		"  WHERE lesser_record_id = duplicate_king($1)"
		"  UNION"
		"  SELECT lesser_record_id FROM alternative_records WHERE greater_record_id = duplicate_king($1)"
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
