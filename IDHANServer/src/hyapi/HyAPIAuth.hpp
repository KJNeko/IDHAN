//
// Created by kj16609 on 11/6/24.
//
#pragma once

#include <drogon/HttpFilter.h>
#include <drogon/HttpMiddleware.h>

namespace idhan::hyapi
{

class HyAPIAuth final : public drogon::HttpCoroFilter< HyAPIAuth >
{
  public:

	HyAPIAuth();

	drogon::Task< std::shared_ptr< drogon::HttpResponse > > doFilter( const drogon::HttpRequestPtr& req ) override;
};

constexpr auto* const HyAPIAuthName { "idhan::hyapi::HyAPIAuth" };
} // namespace idhan::hyapi
