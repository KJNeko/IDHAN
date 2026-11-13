#pragma once

#include <string>
#include <vector>

#include "IDHANTypes.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"
#include "idhan/http/routing/RouteSpec.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::httpapi
{

struct PluginResponse
{
	std::string id;
	std::string name;
	std::string version;
	std::string hostApi;
	http::possiblyMissing< std::string > description {};
	http::possiblyMissing< http::JsonDocument > panels {};
	std::string dir;
	std::string entry;
};

struct ListPluginsQuery
{
	http::possiblyMissing< bool > rescan;
};

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/plugins"}]]
[[= http::Group{"Plugins"}]]
http::HTTPTask< http::JsonResult< std::vector< PluginResponse > > > listPlugins( http::Query< ListPluginsQuery > query );
// clang-format on

} // namespace idhan::httpapi
