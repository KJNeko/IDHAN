//
// Created by claude on 10/15/25.
//
#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#pragma GCC diagnostic ignored "-Wredundant-tags"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wnoexcept"
#pragma GCC diagnostic ignored "-Wredundant-decls"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wnoexcept"
#pragma GCC diagnostic ignored "-Wswitch-enum"
#pragma GCC diagnostic ignored "-Wshadow"
#include <drogon/HttpMiddleware.h>
#pragma GCC diagnostic pop

namespace idhan::hyapi
{

class HyAPIResponseEnricher : public drogon::HttpMiddleware< HyAPIResponseEnricher >
{
  public:

	HyAPIResponseEnricher();

	void invoke(
		const drogon::HttpRequestPtr& req,
		drogon::MiddlewareNextCallback&& nextCb,
		drogon::MiddlewareCallback&& mcb ) override;
};

constexpr auto* RESPONSE_ENRICHER_NAME = "idhan::hyapi::HyAPIResponseEnricher";

} // namespace idhan::hyapi
