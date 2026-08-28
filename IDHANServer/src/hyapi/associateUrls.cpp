#include "HyAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "constants/UrlTypes.hpp"
#include "hyapi/helpers.hpp"
#include "records/records.hpp"
#include "urls/urls.hpp"

namespace idhan::hyapi
{

drogon::Task< std::expected< Json::Value, drogon::HttpResponsePtr > > getAdvancedUrlInfo(
	std::string url_str,
	[[maybe_unused]] DbClientPtr db )
{
	Json::Value root {};

	root[ "request_url" ] = url_str;
	root[ "normalised_url" ] = url_str;
	root[ "url_type" ] = 5; // Unknown URL
	root[ "url_type_string" ] = std::string( urlTypeString( hydrus::gen_constants::URL_TYPE_UNKNOWN ) );
	root[ "match_name" ] = "unknown url";
	root[ "can_parse" ] = false;
	root[ "cannot_parse_reason" ] = "unknown url class";

	co_return root;
}

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::associateUrl( const drogon::HttpRequestPtr request )
{
	const auto json_object { request->getJsonObject() };
	if ( json_object == nullptr ) co_return createBadRequest( "No json data supplied" );
	auto& json { *json_object };

	auto db { drogon::app().getDbClient() };

	const auto records_e { co_await helpers::extractRecordIDsFromFilesJson( json, db ) };
	if ( !records_e ) co_return records_e.error();
	const auto& records { records_e.value() };

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

	if ( !json.isMember( "urls_to_add" ) && !json.isMember( "urls_to_delete" ) )
		co_return createBadRequest( "Did not find any URLs to add or delete" );

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
				co_await db->execSqlCoro(
					"INSERT INTO url_mappings (record_id, url_id) VALUES ($1, $2)", record_id, url_id );
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
				co_await db->execSqlCoro(
					"DELETE FROM url_mappings WHERE record_id = $1 AND url_id = $2", record_id, url_id );
			}
		}
	}

	co_return drogon::HttpResponse::newHttpResponse();
}

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::getUrlInfo( const drogon::HttpRequestPtr request )
{
	const auto url_parameter { request->getOptionalParameter< std::string >( "url" ) };
	if ( !url_parameter ) co_return createBadRequest( "Must provide url parameter" );
	const auto url_str { url_parameter.value() };
	if ( url_str.empty() ) co_return createBadRequest( "Given URL was empty" );

	auto db { drogon::app().getDbClient() };
	const auto url_info_e { co_await getAdvancedUrlInfo( url_str, db ) };
	if ( !url_info_e ) co_return url_info_e.error();
	const auto& url_info { url_info_e.value() };

	co_return drogon::HttpResponse::newHttpJsonResponse( url_info );
}

} // namespace idhan::hyapi
