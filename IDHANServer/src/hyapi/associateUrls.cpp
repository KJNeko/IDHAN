//
// Created by kj16609 on 7/24/25.
//

#include "HyAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/records.hpp"
#include "api/helpers/urls.hpp"
#include "fgl/defines.hpp"
#include "hyapi/helpers.hpp"

namespace idhan::hyapi
{

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::associateUrl( drogon::HttpRequestPtr request )
{
	const auto json_object { request->getJsonObject() };
	if ( json_object == nullptr ) co_return createBadRequest( "No json data supplied" );
	auto& json { *json_object };

	auto db { drogon::app().getDbClient() };

	const auto records_e { co_await helpers::extractRecordIDsFromFilesJson( json, db ) };
	if ( !records_e.has_value() ) co_return records_e.error();
	const auto& records { records_e.value() };

	// change url_to_add to urls_to_add to simplify handling
	if ( json.isMember( "url_to_add" ) )
	{
		json[ "urls_to_add" ] = Json::Value( Json::arrayValue );
		json[ "urls_to_add" ].append( json[ "url_to_add" ].asString() );
	}

	if ( json.isMember( "url_to_delete" ) )
	{
		json[ "urls_to_delete" ] = Json::Value( Json::arrayValue );
		json[ "urls_to_delete" ].append( json[ "url_to_delete" ].asString() );
	}

	if ( json.isMember( "urls_to_add" ) )
	{
		if ( !json[ "urls_to_add" ].isArray() ) co_return createBadRequest( "urls_to_add must be an array" );
		std::vector< UrlID > url_ids {};
		for ( const auto& url : json[ "urls_to_add" ] )
		{
			const auto url_str { url.asString() };
			const auto url_id { co_await idhan::helpers::findOrCreateUrl( url_str, db ) };
			if ( !url_id.has_value() ) co_return url_id.error();
			url_ids.emplace_back( url_id.value() );
		}

		for ( const auto& record_id : records )
		{
			for ( const auto& url_id : url_ids )
			{
				co_await db
					->execSqlCoro( "INSERT INTO url_mappings (record_id, url_id) VALUES ($1, $2)", record_id, url_id );
			}
		}
	}

	if ( json.isMember( "urls_to_delete" ) )
	{
		if ( !json[ "urls_to_delete" ].isArray() ) co_return createBadRequest( "urls_to_delete must be an array" );
		std::vector< UrlID > url_ids {};
		for ( const auto& url : json[ "urls_to_delete" ] )
		{
			const auto url_str { url.asString() };
			const auto url_id { co_await idhan::helpers::findOrCreateUrl( url_str, db ) };
			if ( !url_id.has_value() ) co_return url_id.error();
			url_ids.emplace_back( url_id.value() );
		}

		for ( const auto& record_id : records )
		{
			for ( const auto& url_id : url_ids )
			{
				co_await db
					->execSqlCoro( "DELETE FROM url_mappings WHERE record_id = $1 AND url_id = $2", record_id, url_id );
			}
		}
	}

	co_return drogon::HttpResponse::newHttpResponse();
}

} // namespace idhan::hyapi