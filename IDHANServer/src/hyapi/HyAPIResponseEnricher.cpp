//
// Created by claude on 10/15/25.
//
#include "HyAPIResponseEnricher.hpp"

#include <json/json.h>

#include "ServerContext.hpp"
#include "constants/hydrus_version.hpp"

namespace idhan::hyapi
{
HyAPIResponseEnricher::HyAPIResponseEnricher() = default;

void HyAPIResponseEnricher::invoke(
	const drogon::HttpRequestPtr& req,
	drogon::MiddlewareNextCallback&& nextCb,
	drogon::MiddlewareCallback&& mcb )
{
	// Create a custom callback that intercepts the response
	auto enrichingCallback = [ req ]( const drogon::HttpResponsePtr& resp )
	{
		// Only process JSON responses
		if ( resp->getContentType() != drogon::CT_APPLICATION_JSON ) return resp;

		if ( resp->getStatusCode() != drogon::k200OK ) return resp;

		// Parse the response body
		Json::Value json {};
		Json::Reader reader {};
		const std::string body_str { resp->body() };
		std::string errors {};

		if ( !reader.parse( body_str, json ) )
		{
			return resp;
		}

		// Add hydrus version
		json[ "version" ] = HYDRUS_MIMICED_API_VERSION;
		json[ "hydrus_version" ] = HYDRUS_MIMICED_VERSION;

		// Create new response with enriched JSON
		auto response { drogon::HttpResponse::newHttpJsonResponse( json ) };

		addCORSHeaders( response );

		return response;
	};

	// Wrap the original callback with our enriching callback
	auto wrappedCallback = [ mcb = std::move( mcb ), enrichingCallback ]( const drogon::HttpResponsePtr& resp )
	{ mcb( enrichingCallback( resp ) ); };

	// Continue to next middleware/handler
	nextCb( std::move( wrappedCallback ) );
}
} // namespace idhan::hyapi
