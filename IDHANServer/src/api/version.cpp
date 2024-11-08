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
		json[ "idhan_version" ] = IDHAN_VERSION;
		json[ "idhan_api_version" ] = IDHAN_API_VERSION;
		json[ "hydrus_api_version" ] = HYDRUS_MIMICED_API_VERSION;

		callback( drogon::HttpResponse::newHttpJsonResponse( json ) );
	}

} // namespace idhan::api