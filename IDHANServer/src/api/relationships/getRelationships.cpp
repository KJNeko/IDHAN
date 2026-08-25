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
		"WITH RECURSIVE inferior(record_id) AS ("
		"  SELECT worse_record_id FROM duplicate_pairs WHERE better_record_id = $1"
		"  UNION"
		"  SELECT pair.worse_record_id FROM duplicate_pairs pair"
		"  JOIN inferior ON pair.better_record_id = inferior.record_id"
		") SELECT record_id FROM inferior ORDER BY record_id",
		record_id ) };
	auto superior_result { db->execSqlCoro(
		"WITH RECURSIVE superior(record_id) AS ("
		"  SELECT better_record_id FROM duplicate_pairs WHERE worse_record_id = $1"
		"  UNION"
		"  SELECT pair.better_record_id FROM duplicate_pairs pair"
		"  JOIN superior ON pair.worse_record_id = superior.record_id"
		") SELECT record_id FROM superior ORDER BY record_id",
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
