//
// Created by kj16609 on 7/24/25.
//

#include "urls/urls.hpp"
#include "IDHANTypes.hpp"
#include "api/RecordAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "db/drogonArrayBind.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > RecordAPI::addUrls( drogon::HttpRequestPtr request, RecordID record_id )
{
	const auto db { drogon::app().getDbClient() };

	const auto json_object { request->getJsonObject() };
	if ( !json_object ) co_return createBadRequest( "Json object malformed or null" );

	const auto& json { *json_object };

	// operator[] on a non-object root throws Json::LogicError, which would surface as a 500
	if ( !json.isObject() ) co_return createBadRequest( "Invalid json object. Expected object as root item" );

	const auto& urls { json[ "urls" ] };
	if ( !urls.isArray() ) co_return createBadRequest( "No urls array in json" );

	if ( urls.empty() )
	{
		Json::Value result {};
		result[ "status" ] = drogon::k200OK;
		co_return drogon::HttpResponse::newHttpJsonResponse( result );
	}

	std::vector< std::string > url_strings;
	std::vector< std::string > domain_strings;
	url_strings.reserve( urls.size() );
	domain_strings.reserve( urls.size() );

	for ( const auto& url : urls )
	{
		if ( !url.isString() ) co_return createBadRequest( "Invalid item in urls array: Expected string" );

		auto url_str { url.asString() };
		domain_strings.push_back( helpers::extractDomain( url_str ) );
		url_strings.push_back( std::move( url_str ) );
	}

	// 1. Batch upsert all domains (copy — domain_strings needed again in step 2)
	co_await db->execSqlCoro(
		"INSERT INTO url_domains (url_domain) "
		"SELECT DISTINCT unnest($1::text[]) "
		"ON CONFLICT (url_domain) DO NOTHING",
		std::vector< std::string >( domain_strings ) );

	// 2. Batch upsert all URLs, resolving domain IDs via the just-inserted rows
	co_await db->execSqlCoro(
		"INSERT INTO urls (url, url_domain_id) "
		"SELECT pairs.url, ud.url_domain_id "
		"FROM unnest($1::text[], $2::text[]) AS pairs(url, domain) "
		"JOIN url_domains ud ON ud.url_domain = pairs.domain "
		"ON CONFLICT (url) DO NOTHING",
		std::vector< std::string >( url_strings ),
		std::move( domain_strings ) );

	// 3. Batch insert url_mappings for all URLs (whether just created or pre-existing)
	co_await db->execSqlCoro(
		"INSERT INTO url_mappings (url_id, record_id) "
		"SELECT u.url_id, $2 "
		"FROM urls u "
		"WHERE u.url = ANY($1::text[]) "
		"ON CONFLICT DO NOTHING",
		std::move( url_strings ),
		record_id );

	Json::Value result {};
	result[ "status" ] = drogon::k200OK;
	co_return drogon::HttpResponse::newHttpJsonResponse( result );
}

} // namespace idhan::api
