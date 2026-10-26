#pragma once

#include "idhan/http/RouteSpec.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::httpapi
{

[[nodiscard]] http::RouteDefinition rateLimitListRoute( db::DBContext& db );

[[nodiscard]] http::RouteDefinition rateLimitResetRoute( db::DBContext& db );

} // namespace idhan::httpapi
