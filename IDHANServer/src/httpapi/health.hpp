#pragma once

#include "IDHANTypes.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"
#include "idhan/http/routing/RouteSpec.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::httpapi
{

struct HealthResponse
{
	std::string status;
};

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/health"}]]
[[= http::Group{"System"}]]
[[= http::Public {}]]
[[nodiscard]] inline http::HTTPTask< http::JsonResult< HealthResponse > > health()
{
	co_return http::JsonResponse< HealthResponse > { { "ok" } };
}
// clang-format on

} // namespace idhan::httpapi
