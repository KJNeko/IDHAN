#pragma once

#include "idhan/http/RouteSpec.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::httpapi
{

[[nodiscard]] http::RouteDefinition searchRoute( db::DBContext& db );

[[nodiscard]] http::RouteDefinition searchPostRoute( db::DBContext& db );

} // namespace idhan::httpapi
