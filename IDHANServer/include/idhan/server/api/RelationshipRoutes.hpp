#pragma once

#include "ApiRouteGroup.hpp"

namespace idhan::server::api
{

class RelationshipRoutes final : public ApiRouteGroup
{
  public:

	void addRoutes( http::RouteRegistry& ) const override;
	[[nodiscard]] static http::HTTPTask< http::Response > relationships( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > similar( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > nextUndecided( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > setBetterDuplicate( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > addAlternative( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > clear( http::RequestContext );
	[[nodiscard]] static http::HTTPTask< http::Response > setUnrelated( http::RequestContext );
};

} // namespace idhan::server::api
