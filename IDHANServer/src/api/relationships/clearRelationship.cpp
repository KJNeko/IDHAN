#include <expected>

#include "IDHANTypes.hpp"
#include "api/FileRelationshipsAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"

namespace idhan::api
{

std::expected< std::pair< RecordID, RecordID >, drogon::HttpResponsePtr > parseRecordPair( const Json::Value& object )
{
	// isMember() throws Json::LogicError on non-objects, which would surface as a 500
	if ( !object.isObject() )
		return std::unexpected( createBadRequest( "Expected object with `record_id_a` and `record_id_b`" ) );

	if ( !object.isMember( "record_id_a" ) || !object.isMember( "record_id_b" ) )
		return std::unexpected( createBadRequest( "Expected object to have `record_id_a` and `record_id_b`" ) );

	if ( !object[ "record_id_a" ].isUInt() || !object[ "record_id_b" ].isUInt() )
		return std::unexpected(
			createBadRequest( "Expected `record_id_a` and `record_id_b` to be unsigned integers" ) );

	const RecordID record_id_a { object[ "record_id_a" ].as< RecordID >() };
	const RecordID record_id_b { object[ "record_id_b" ].as< RecordID >() };

	if ( record_id_a == record_id_b )
		return std::unexpected( createBadRequest( "`record_id_a` and `record_id_b` cannot be the same record" ) );

	return std::pair { record_id_a, record_id_b };
}

drogon::Task< drogon::HttpResponsePtr > FileRelationshipsAPI::clearRelationship( drogon::HttpRequestPtr request )
{
	const auto json_ptr { request->getJsonObject() };
	if ( !json_ptr ) co_return createBadRequest( "Expected json body" );

	const auto pair { parseRecordPair( *json_ptr ) };
	if ( !pair ) co_return pair.error();

	const auto [ record_id_a, record_id_b ] { *pair };

	const auto db { drogon::app().getDbClient() };

	const auto validation { co_await helpers::validateRecordIds( { record_id_a, record_id_b }, db ) };
	if ( !validation ) co_return validation.error();

	const auto duplicate { co_await db->execSqlCoro( "SELECT remove_duplicate_pair($1, $2) AS removed", record_id_a,
		record_id_b ) };

	const auto unrelated { co_await db->execSqlCoro(
		"DELETE FROM unrelated_records WHERE lesser_record_id = LEAST($1::integer, $2::integer) "
		"AND greater_record_id = GREATEST($1::integer, $2::integer)",
		record_id_a,
		record_id_b ) };

	const auto alternative { co_await db->execSqlCoro(
		"DELETE FROM alternative_records WHERE lesser_record_id = LEAST($1::integer, $2::integer) "
		"AND greater_record_id = GREATEST($1::integer, $2::integer)",
		record_id_a,
		record_id_b ) };

	Json::Value response {};
	response[ "duplicate_removed" ] = duplicate[ 0 ][ "removed" ].as< bool >();
	response[ "alternative_removed" ] = alternative.affectedRows() > 0;
	response[ "unrelated_removed" ] = unrelated.affectedRows() > 0;

	co_return drogon::HttpResponse::newHttpJsonResponse( response );
}

} // namespace idhan::api
