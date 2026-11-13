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

struct VersionInfo
{
	struct Number
	{
		std::string string;
		std::size_t major, minor, patch;
	};

	Number idhan_server_version;
	Number idhan_api_version;

	std::string branch;
	std::string commit;
	std::string tag;
	std::string build;
	bool unsynced;
	std::string build_on;
};

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/version"}]]
[[= http::Group{"System"}]]
[[= http::Public {}]]
[[= http::Description("Returns version information")]]
[[nodiscard]] http::HTTPTask< http::JsonResult< VersionInfo > > version( [[maybe_unused]] http::RequestContext context );
// clang-format on

} // namespace idhan::httpapi
