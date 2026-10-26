#pragma once

#include "idhan/http/RouteSpec.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::httpapi
{

[[nodiscard]] http::RouteDefinition downloaderDebugRoute( db::DBContext& db );
[[nodiscard]] http::RouteDefinition eventsRoute( db::DBContext& db );

}
