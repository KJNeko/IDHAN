#include <algorithm>

#include "IDHANTypes.hpp"
#include "api/FileRelationshipsAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"

namespace idhan::api
{

std::expected< std::pair< RecordID, RecordID >, drogon::HttpResponsePtr > parseRecordPair( const Json::Value& object );

drogon::Task< drogon::HttpResponsePtr > FileRelationshipsAPI::setUnrelated( drogon::HttpRequestPtr request )
{
	const auto json_ptr { request->getJsonObject() };
	if ( !json_ptr ) co_return createBadRequest( "Expected json body" );

	const auto pair { parseRecordPair( *json_ptr ) };
	if ( !pair ) co_return pair.error();

	const auto [ lesser, greater ] { std::minmax( pair->first, pair->second ) };

	const auto db { drogon::app().getDbClient() };

	const auto validation { co_await helpers::validateRecordIds( { lesser, greater }, db ) };
	if ( !validation ) co_return validation.error();

	co_await db->execSqlCoro(
		"INSERT INTO unrelated_records (lesser_record_id, greater_record_id) VALUES ($1, $2) "
		"ON CONFLICT DO NOTHING",
		lesser,
		greater );

	co_return drogon::HttpResponse::newHttpJsonResponse( {} );
}

} // namespace idhan::api
