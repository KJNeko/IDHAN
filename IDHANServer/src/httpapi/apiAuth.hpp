#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "IDHANTypes.hpp"
#include "idhan/http/openapi/OpenAPIAuthentication.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"
#include "idhan/http/routing/RouteSpec.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::httpapi
{

inline constexpr std::array< std::string_view, 3 > apiKeyHeaders { { "Authorization", "X-API-Key", "IDHAN-API-Key" } };
#ifdef IDHAN_DISABLE_API_AUTH
inline constexpr http::OpenAPIAuthentication nativeOpenAPIAuthentication {};
#else
inline constexpr http::OpenAPIAuthentication nativeOpenAPIAuthentication { apiKeyHeaders, "idhan_key" };
#endif

struct KeyRejection
{
	std::uint16_t status;
	std::string message;
};

[[nodiscard]] http::HTTPTask< std::optional< KeyRejection > > verifyApiKey( std::string_view key, db::DBContext& db );

[[nodiscard]] http::RequestValidation apiAuthValidation( db::DBContext& db );

struct AuthenticationResponse
{
	bool authenticated { true };
};

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/auth/verify"}]]
[[= http::Group{"Authentication"}]]
http::HTTPTask< http::JsonResult< AuthenticationResponse > > verifyAuthentication( http::RequestContext );
// clang-format on

} // namespace idhan::httpapi
