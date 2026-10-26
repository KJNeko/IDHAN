#pragma once

#include "idhan/http/RouteSpec.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::httpapi
{

[[nodiscard]] http::RouteDefinition generateApiKeyRoute( db::DBContext& db );

}
