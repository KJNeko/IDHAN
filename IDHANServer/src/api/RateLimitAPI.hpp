#pragma once

#include <drogon/HttpController.h>

#include "api/APIAuth.hpp"

namespace idhan::api
{
class RateLimitAPI final : public drogon::HttpController< RateLimitAPI >
{
	static drogon::Task< drogon::HttpResponsePtr > list( drogon::HttpRequestPtr request );
	static drogon::Task< drogon::HttpResponsePtr > reset( drogon::HttpRequestPtr request );

  public:

	METHOD_LIST_BEGIN
	ADD_METHOD_TO( RateLimitAPI::list, "/rate_limits", drogon::Get, IDHANAPIAuthName );
	ADD_METHOD_TO( RateLimitAPI::reset, "/rate_limits/reset", drogon::Post, IDHANAPIAuthName );
	METHOD_LIST_END
};

} // namespace idhan::api
