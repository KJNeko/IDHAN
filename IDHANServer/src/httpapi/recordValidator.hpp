#pragma once

#include "idhan/http/RouteSpec.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::httpapi
{

[[nodiscard]] http::PathParameter recordIdPathParameter();

[[nodiscard]] http::PathParameter noteIdPathParameter();

[[nodiscard]] http::RequestValidation recordExistsValidation( db::DBContext& db );

} // namespace idhan::httpapi
