//
// Created by kj16609 on 6/12/25.
//

#include "IDHANTypes.hpp"
#include "api/RecordAPI.hpp"
#include "api/helpers/createBadRequest.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > RecordAPI::getRandomActiveRecord( drogon::HttpRequestPtr request )
{
	const auto db { drogon::app().getDbClient() };

	const auto result { co_await db->execSqlCoro(
		"SELECT record_id FROM file_info WHERE mime_id IS NOT NULL ORDER BY RANDOM() LIMIT 1" ) };

	if ( result.empty() ) co_return createBadRequest( "No active records found" );

	Json::Value root {};
	root[ "record_id" ] = result[ 0 ][ 0 ].as< RecordID >();

	co_return drogon::HttpResponse::newHttpJsonResponse( root );
}

} // namespace idhan::api
