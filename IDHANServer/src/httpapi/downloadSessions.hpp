#pragma once

#include <vector>

#include "idhan/http/RouteSpec.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::httpapi
{

[[nodiscard]] std::vector< http::RouteDefinition > downloadSessionRoutes( db::DBContext& db );

}
