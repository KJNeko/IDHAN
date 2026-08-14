#pragma once

#include <drogon/HttpFilter.h>
#include <drogon/HttpTypes.h>

namespace idhan::hyapi
{

//! Converts Hydrus `hashes` parameters to record IDs before the handler runs.
class HyAPIHashConversion : public drogon::HttpCoroMiddleware< HyAPIHashConversion >
{
  public:

	drogon::Task< drogon::HttpResponsePtr > invoke(
		const drogon::HttpRequestPtr& request,
		drogon::MiddlewareNextAwaiter&& next ) override;
};

} // namespace idhan::hyapi
