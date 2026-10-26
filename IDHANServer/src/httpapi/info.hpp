#pragma once

#include "idhan/http/RouteSpec.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::httpapi
{

[[nodiscard]] http::RouteDefinition apiDocsIndexRoute();

[[nodiscard]] http::RouteDefinition logRoute( db::DBContext& db );

} // namespace idhan::httpapi
