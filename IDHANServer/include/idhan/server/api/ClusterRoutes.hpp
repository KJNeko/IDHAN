#pragma once

#include "ApiRouteGroup.hpp"

namespace idhan::server::api
{

class ClusterRoutes final : public ApiRouteGroup
{
  public:

	void addRoutes( http::RouteRegistry& ) const override;
	[[nodiscard]] static http::HTTPTask< http::Response > add( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > list( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > info( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > modify( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > remove( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > scan( http::RequestContext );
};

} // namespace idhan::server::api
