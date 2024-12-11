//
// Created by kj16609 on 11/8/24.
//

#include "IDHANApi.hpp"
#include "hyapi/constants/hydrus_version.hpp"
#include "logging/log.hpp"
#include "versions.hpp"

namespace idhan::api
{

void IDHANApi::version( const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
{
	log::debug( "/version" );

	Json::Value json;

	json[ "idhan_server_version" ][ "string" ] =
		std::format( "{}.{}.{}", IDHAN_SERVER_MAJOR, IDHAN_SERVER_MINOR, IDHAN_SERVER_PATCH );
	json[ "idhan_server_version" ][ "major" ] = IDHAN_SERVER_MAJOR;
	json[ "idhan_server_version" ][ "minor" ] = IDHAN_SERVER_MINOR;
	json[ "idhan_server_version" ][ "patch" ] = IDHAN_SERVER_PATCH;

	json[ "idhan_api_version" ][ "string" ] =
		std::format( "{}.{}.{}", IDHAN_API_MAJOR, IDHAN_API_MINOR, IDHAN_API_PATCH );
	json[ "idhan_api_version" ][ "major" ] = IDHAN_API_MAJOR;
	json[ "idhan_api_version" ][ "minor" ] = IDHAN_API_MINOR;
	json[ "idhan_api_version" ][ "patch" ] = IDHAN_API_PATCH;

	json[ "hydrus_api_version" ] = HYDRUS_MIMICED_API_VERSION;

	callback( drogon::HttpResponse::newHttpJsonResponse( json ) );
}

} // namespace idhan::api