//
// Created by kj16609 on 11/6/24.
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
class HyAPIAuth : public drogon::HttpMiddleware< HyAPIAuth >
{
  public:

	HyAPIAuth();

	void invoke(
		const drogon::HttpRequestPtr& req,
		drogon::MiddlewareNextCallback&& nextCb,
		drogon::MiddlewareCallback&& mcb ) override;
};

constexpr auto* const HyAPIAuthName { "idhan::hyapi::HyAPIAuth" };
} // namespace idhan::hyapi
