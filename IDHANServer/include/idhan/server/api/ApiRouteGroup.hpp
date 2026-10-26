#pragma once

#include <vector>

#include "idhan/http/RouteRegistry.hpp"

namespace idhan::server::api
{

using RouteList = std::vector< http::RouteDefinition >;

class ApiRouteGroup
{
  public:

	virtual ~ApiRouteGroup();
	ApiRouteGroup();
	ApiRouteGroup( const ApiRouteGroup& ) = delete;
	ApiRouteGroup& operator=( const ApiRouteGroup& ) = delete;

	virtual void addRoutes( http::RouteRegistry& ) const = 0;
};

void registerApiRoutes( http::RouteRegistry&, const std::vector< const ApiRouteGroup* >& );

} // namespace idhan::server::api
