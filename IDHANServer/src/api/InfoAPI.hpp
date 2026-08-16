#pragma once
#include "APIAuth.hpp"
#include "drogon/HttpController.h"

namespace idhan::api
{

class InfoAPI : public drogon::HttpController< InfoAPI >
{
	drogon::Task< drogon::HttpResponsePtr > api( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > apiDocs( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > version( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > log( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > health( drogon::HttpRequestPtr request );

  public:

	METHOD_LIST_BEGIN

	ADD_METHOD_TO( InfoAPI::api, "/api", drogon::Get );
	ADD_METHOD_VIA_REGEX( InfoAPI::apiDocs, "/api/.*\\.yaml", drogon::Get );

	ADD_METHOD_TO( InfoAPI::version, "/version", drogon::Get );

	// unlike /version and /health this exposes full trace-level logs, so it needs the api key
	ADD_METHOD_TO( InfoAPI::log, "/log", drogon::Get, IDHANAPIAuthName );

	ADD_METHOD_TO( InfoAPI::health, "/health", drogon::Get );

	METHOD_LIST_END
};

} // namespace idhan::api
