#pragma once

#include "ApiRouteGroup.hpp"

namespace idhan::server::api
{

class DownloaderRoutes final : public ApiRouteGroup
{
  public:

	void addRoutes( http::RouteRegistry& ) const override;
	[[nodiscard]] static http::HTTPTask< http::Response > listSessions( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > createSession( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > session( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > destroySession( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > submitSessionUrls( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > submitUrl( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > sessionUrls( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > retryUrl( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > sessionRecords( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > sessionErrors( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > debug( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > listSecrets( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > setSecrets( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > listRateLimits( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > resetRateLimit( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > sessionEvents( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > rateLimitEvents( http::RequestContext );
};

} // namespace idhan::server::api
