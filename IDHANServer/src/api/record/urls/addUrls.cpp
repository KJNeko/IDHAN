#include "IDHANTypes.hpp"
#include "api/RecordAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "db/drogonArrayBind.hpp"
#include "urls/urls.hpp"

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

	std::vector< std::string > url_strings {};
	url_strings.reserve( urls.size() );

	for ( const auto& url : urls )
	{
		if ( !url.isString() ) co_return createBadRequest( "Invalid item in urls array: Expected string" );

		auto url_str { url.asString() };
		url_strings.push_back( std::move( url_str ) );
	}

	const auto associated { co_await helpers::associateUrls( record_id, std::move( url_strings ), db ) };
	if ( !associated ) co_return associated.error();

	Json::Value result {};
	result[ "status" ] = drogon::k200OK;
	co_return drogon::HttpResponse::newHttpJsonResponse( result );
}

} // namespace idhan::api
