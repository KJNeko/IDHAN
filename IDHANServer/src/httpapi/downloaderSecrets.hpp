#pragma once

#include "idhan/http/RouteSpec.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::httpapi
{

[[nodiscard]] http::RouteDefinition downloaderSecretsListRoute( db::DBContext& db );

[[nodiscard]] http::RouteDefinition downloaderSecretsSetRoute( db::DBContext& db );

} // namespace idhan::httpapi
