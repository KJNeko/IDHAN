//
// Created by kj16609 on 11/8/24.
//

#include "version.hpp"

#include <paths.hpp>

#include "InfoAPI.hpp"
#include "hyapi/constants/hydrus_version.hpp"
#include "idhan/versions.hpp"
#include "logging/log.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > InfoAPI::version( [[maybe_unused]] drogon::HttpRequestPtr request )
{
	log::debug( "/version" );

	Json::Value json;

	json[ "idhan_server_version" ][ "string" ] =
		format_ns::format( "{}.{}.{}", IDHAN_MAJOR_VERSION, IDHAN_MINOR_VERSION, IDHAN_PATCH_VERSION );
	json[ "idhan_server_version" ][ "major" ] = IDHAN_MAJOR_VERSION;
	json[ "idhan_server_version" ][ "minor" ] = IDHAN_MINOR_VERSION;
	json[ "idhan_server_version" ][ "patch" ] = IDHAN_PATCH_VERSION;

	json[ "idhan_api_version" ][ "string" ] =
		format_ns::format( "{}.{}.{}", IDHAN_API_MAJOR, IDHAN_API_MINOR, IDHAN_API_PATCH );
	json[ "idhan_api_version" ][ "major" ] = IDHAN_API_MAJOR;
	json[ "idhan_api_version" ][ "minor" ] = IDHAN_API_MINOR;
	json[ "idhan_api_version" ][ "patch" ] = IDHAN_API_PATCH;

	json[ "hydrus_api_version" ] = HYDRUS_MIMICED_API_VERSION;
	json[ "hydrus_version" ] = HYDRUS_MIMICED_VERSION;

	json[ "branch" ] = FGL_GIT_BRANCH;
	json[ "commit" ] = FGL_GIT_COMMIT;
	json[ "tag" ] = FGL_GIT_TAG;
	json[ "build" ] = FGL_BUILD_TYPE;
	json[ "build_on" ] = IDHAN_BUILD_DATE ", " IDHAN_BUILD_TIME;

	co_return drogon::HttpResponse::newHttpJsonResponse( json );
}

} // namespace idhan::api
