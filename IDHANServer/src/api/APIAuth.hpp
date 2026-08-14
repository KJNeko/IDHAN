#pragma once

#include "drogon/HttpController.h"
#include "drogon/HttpMiddleware.h"

namespace idhan::api
{

class APIAuth : public drogon::HttpCoroFilter< APIAuth >
{
  public:

	APIAuth() = default;

	drogon::Task< drogon::HttpResponsePtr > doFilter( const drogon::HttpRequestPtr& req ) override;
};

constexpr auto IDHANAPIAuthName { "idhan::api::APIAuth" };

class AuthEndpoint final : public drogon::HttpController< AuthEndpoint >
{
	drogon::Task< drogon::HttpResponsePtr > verifyAccessKey( drogon::HttpRequestPtr req );
	drogon::Task< drogon::HttpResponsePtr > generateApiKey( drogon::HttpRequestPtr req );

	drogon::Task< drogon::HttpResponsePtr > verifyKey( drogon::HttpRequestPtr req );

  public:

	METHOD_LIST_BEGIN
	ADD_METHOD_TO( AuthEndpoint::verifyAccessKey, "/hyapi/verify_access_key", drogon::Get );
	ADD_METHOD_TO( AuthEndpoint::generateApiKey, "/generate_api_key" );
	ADD_METHOD_TO( AuthEndpoint::verifyKey, "/auth/verify", drogon::Get, IDHANAPIAuthName );
	METHOD_LIST_END
};

} // namespace idhan::api
