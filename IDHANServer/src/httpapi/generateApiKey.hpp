#pragma once

#include <string>

#include "IDHANTypes.hpp"
#include "idhan/http/response/ErrorResponse.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"
#include "idhan/http/routing/RouteSpec.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::httpapi
{

struct GeneratedApiKeyResponse
{
	std::string key;
};

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/generate_api_key"}]]
[[= http::Group{"Authentication"}]]
[[= http::Public {}]]
[[= http::Output<http::ErrorResponse>{404}]]
http::HTTPTask< http::JsonResult< GeneratedApiKeyResponse > > generateApiKey( db::DBContext& db, [[maybe_unused]] http::RequestContext context );
// clang-format on

} // namespace idhan::httpapi
