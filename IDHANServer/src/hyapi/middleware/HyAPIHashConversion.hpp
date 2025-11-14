//
// Created by kj16609 on 11/13/25.
//
#pragma once

#include <drogon/HttpFilter.h>
#include <drogon/HttpTypes.h>

namespace idhan::hyapi
{

/**
 * @brief Middleware that converts 'hashes' parameter to 'hash_ids' parameter
 *
 * This filter intercepts requests containing a 'hashes' parameter (JSON array of SHA256 hex strings)
 * and converts them to 'hash_ids' (JSON array of record IDs) by looking them up in the database.
 * The mapping is done using the records table (sha256, record_id).
 */
class HyAPIHashConversion : public drogon::HttpCoroMiddleware< HyAPIHashConversion >
{
  public:

	drogon::Task< drogon::HttpResponsePtr > invoke(
		const drogon::HttpRequestPtr& request,
		drogon::MiddlewareNextAwaiter&& next ) override;
};

} // namespace idhan::hyapi