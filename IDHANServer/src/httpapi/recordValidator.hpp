#pragma once

#include "idhan/http/routing/RouteSpec.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::httpapi
{

[[nodiscard]] http::RequestValidation recordExistsValidation( db::DBContext& db );

} // namespace idhan::httpapi
