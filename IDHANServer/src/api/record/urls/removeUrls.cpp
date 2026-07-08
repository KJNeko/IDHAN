//
// Created by kj16609 on 7/24/25.
//

#include "IDHANTypes.hpp"
#include "api/RecordAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "db/drogonArrayBind.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > RecordAPI::removeUrls( drogon::HttpRequestPtr request, RecordID record_id )
{
	const auto db { drogon::app().getDbClient() };

	const auto json_object { request->getJsonObject() };
	if ( !json_object ) co_return createBadRequest( "Json object malformed or null" );

	const auto& json { *json_object };

	// operator[] on a non-object root throws Json::LogicError, which would surface as a 500
	if ( !json.isObject() ) co_return createBadRequest( "Invalid json object. Expected object as root item" );

	const auto& urls { json[ "urls" ] };
	if ( !urls.isArray() ) co_return createBadRequest( "No urls array in json" );

	std::vector< std::string > url_strings;
	url_strings.reserve( urls.size() );
	for ( const auto& url : urls )
	{
		if ( !url.isString() ) co_return createBadRequest( "Invalid item in urls array: Expected string" );
		url_strings.push_back( url.asString() );
	}

	co_await db->execSqlCoro(
		"DELETE FROM url_mappings um "
		"USING urls u "
		"WHERE u.url = ANY($1::text[]) "
		"  AND um.url_id = u.url_id "
		"  AND um.record_id = $2",
		std::move( url_strings ),
		record_id );

	co_return drogon::HttpResponse::newHttpResponse();
}

} // namespace idhan::api
