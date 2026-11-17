#pragma once

#include "idhan/http/routing/RouteRegistry.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::httpapi
{

void registerNativeRoutes( http::RouteRegistry&, db::DBContext& );

} // namespace idhan::httpapi
