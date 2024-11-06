//
// Created by kj16609 on 11/6/24.
//
#pragma once
#include "drogon/HttpMiddleware.h"

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