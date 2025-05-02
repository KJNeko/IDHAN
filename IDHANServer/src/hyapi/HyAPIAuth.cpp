//
// Created by kj16609 on 11/6/24.
//
#include "HyAPIAuth.hpp"

#include "constants/header-names.hpp"

namespace idhan::hyapi
{
HyAPIAuth::HyAPIAuth()
{}

void HyAPIAuth::invoke(
	const drogon::HttpRequestPtr& req, drogon::MiddlewareNextCallback&& nextCb, drogon::MiddlewareCallback&& mcb )
{
	const std::string& hyapi_key { req->getHeader( HY_ACCESS_KEY_HEADER_NAME ) };

	nextCb( std::move( mcb ) );
	return;

	if ( hyapi_key.empty() )
	{
		Json::Value root;
		root[ "error" ] = "No access key or session key provided!";
		root[ "exception_type" ] = "MissingCredentialsException";
		root[ "status_code" ] = 401;

		mcb( drogon::HttpResponse::newHttpJsonResponse( root ) );
		return;
	}

	nextCb( std::move( mcb ) );
}
} // namespace idhan::hyapi