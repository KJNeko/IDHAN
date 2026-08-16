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
	auto enrichingCallback = [ req ]( const drogon::HttpResponsePtr& resp )
	{
		if ( resp->getContentType() != drogon::CT_APPLICATION_JSON ) return resp;

		if ( resp->getStatusCode() != drogon::k200OK ) return resp;

		Json::Value json {};
		Json::Reader reader {};
		const std::string body_str { resp->body() };
		std::string errors {};

		if ( !reader.parse( body_str, json ) )
		{
			return resp;
		}

		json[ "version" ] = HYDRUS_MIMICED_API_VERSION;
		json[ "hydrus_version" ] = HYDRUS_MIMICED_VERSION;

		auto response { drogon::HttpResponse::newHttpJsonResponse( json ) };

		addCORSHeaders( response );

		return response;
	};

	auto wrappedCallback = [ mcb = std::move( mcb ), enrichingCallback ]( const drogon::HttpResponsePtr& resp )
	{ mcb( enrichingCallback( resp ) ); };

	nextCb( std::move( wrappedCallback ) );
}
} // namespace idhan::hyapi
