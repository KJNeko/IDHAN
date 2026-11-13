#pragma once

#include <string>

#include "idhan/http/routing/RouteSpec.hpp"

namespace idhan::httpapi
{

[[nodiscard]] http::RouteDefinition indexRoute( std::string path );

[[nodiscard]] http::RouteDefinition assetsRoute();

[[nodiscard]] http::RouteDefinition pluginFilesRoute();

} // namespace idhan::httpapi
