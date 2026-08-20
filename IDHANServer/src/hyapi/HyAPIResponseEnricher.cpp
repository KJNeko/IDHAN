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
	auto enrichingCallback = []( const drogon::HttpResponsePtr& resp )
	{
		if ( resp->getContentType() != drogon::CT_APPLICATION_JSON ) return resp;

		if ( resp->getStatusCode() != drogon::k200OK ) return resp;

		// Reading the body would serialize the tree, and it is still unserialized here, so the two keys go
		// straight into it. Touching resp->body() instead costs a serialize, a copy, a reparse and a second
		// tree, all of which scale with the response.
		const auto& json { resp->getJsonObject() };

		if ( !json ) return resp;

		( *json )[ "version" ] = HYDRUS_MIMICED_API_VERSION;
		( *json )[ "hydrus_version" ] = HYDRUS_MIMICED_VERSION;

		addCORSHeaders( resp );

		return resp;
	};

	auto wrappedCallback = [ mcb = std::move( mcb ), enrichingCallback ]( const drogon::HttpResponsePtr& resp )
	{ mcb( enrichingCallback( resp ) ); };

	nextCb( std::move( wrappedCallback ) );
}
} // namespace idhan::hyapi
