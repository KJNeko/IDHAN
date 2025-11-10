//
// Created by kj16609 on 7/24/25.
//

#include <expected>

#include "IDHANTypes.hpp"
#include "api/RecordAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/urls.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > RecordAPI::addUrls( drogon::HttpRequestPtr request, RecordID record_id )
{
	const auto db { drogon::app().getDbClient() };

	const auto json_object { request->getJsonObject() };
	if ( !json_object ) co_return createBadRequest( "Json object malformed or null" );

	const auto& json { *json_object };
	const auto& urls { json[ "urls" ] };
	if ( !urls.isArray() ) co_return createBadRequest( "No urls array in json" );

	for ( const auto& url : urls )
	{
		const auto url_id { co_await helpers::findOrCreateUrl( url.asString(), db ) };

		if ( !url_id ) co_return url_id.error();

		co_await db->execSqlCoro(
			"INSERT INTO url_mappings (url_id, record_id) VALUES ($1, $2) ON CONFLICT DO NOTHING", *url_id, record_id );
	}

	Json::Value result {};
	result[ "status" ] = drogon::k200OK;

	co_return drogon::HttpResponse::newHttpJsonResponse( result );
}

} // namespace idhan::api
