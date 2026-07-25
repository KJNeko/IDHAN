//
// Created by kj16609 on 11/8/24.
//

#include "api/version.hpp"

#include <paths.hpp>

#include "api/InfoAPI.hpp"
#include "hyapi/constants/hydrus_version.hpp"
#include "idhan/versions.hpp"
#include "logging/log.hpp"
#include "profiling/tracy.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > InfoAPI::version( [[maybe_unused]] drogon::HttpRequestPtr request )
{
	ZoneScoped;
	log::debug( "/version" );

	Json::Value json;

	{
		auto version_str =
			format_ns::format( "{}.{}.{}", IDHAN_MAJOR_VERSION, IDHAN_MINOR_VERSION, IDHAN_PATCH_VERSION );

#if defined( FGL_GIT_UNSYNCED ) && FGL_GIT_UNSYNCED == 1
		version_str += "-tainted";
#endif

		json[ "idhan_server_version" ][ "string" ] = version_str;
	}
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
	json[ "unsynced" ] = FGL_GIT_UNSYNCED;
	json[ "build_on" ] = IDHAN_BUILD_DATE ", " IDHAN_BUILD_TIME;

	// Whether this build was compiled with Tracy profiler instrumentation (IDHAN_ENABLE_TRACY). Lets a
	// client tell if it can connect a Tracy profiler to this server (port 8086) without guessing.
#ifdef TRACY_ENABLE
	json[ "tracy_enabled" ] = true;
#else
	json[ "tracy_enabled" ] = false;
#endif

	co_return drogon::HttpResponse::newHttpJsonResponse( json );
}

} // namespace idhan::api
