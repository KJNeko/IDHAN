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

drogon::Task< std::expected< Json::Value, drogon::HttpResponsePtr > >
	getAdvancedUrlInfo( std::string url_str, drogon::orm::DbClientPtr db )
{
	Json::Value root {};

	// const auto url_id { co_await idhan::helpers::findOrCreateUrl( url_str, db ) };
	// if ( !url_id ) co_return url_id.error();
	// const auto url_id_e { url_id.value() };

	root[ "request_url" ] = url_str;
	root[ "normalized_url" ] = url_str;
	root[ "url_type" ] = 5; // Unknown URL
	root[ "url_type_string" ] = "unknown";
	// root["match_name"] =
	root[ "can_parse" ] = false;

	co_return root;
}

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::getUrlInfo( drogon::HttpRequestPtr request )
{
	const auto url_parameter { request->getOptionalParameter< std::string >( "url" ) };
	if ( !url_parameter ) co_return createBadRequest( "Must provide url parameter" );
	const auto url_str { url_parameter.value() };

	auto db { drogon::app().getDbClient() };
	const auto url_info_e { co_await getAdvancedUrlInfo( url_str, db ) };
	if ( !url_info_e ) co_return url_info_e.error();
	const auto& url_info { url_info_e.value() };

	co_return drogon::HttpResponse::newHttpJsonResponse( url_info );
}

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::associateUrl( drogon::HttpRequestPtr request )
{
	const auto json_object { request->getJsonObject() };
	if ( json_object == nullptr ) co_return createBadRequest( "No json data supplied" );
	auto& json { *json_object };

	auto db { drogon::app().getDbClient() };

	const auto records_e { co_await helpers::extractRecordIDsFromFilesJson( json, db ) };
	if ( !records_e ) co_return records_e.error();
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
			if ( !url_id ) co_return url_id.error();
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
			if ( !url_id ) co_return url_id.error();
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