#pragma once

#include <string>

#include "idhan/http/RouteSpec.hpp"

namespace idhan::httpapi
{

[[nodiscard]] http::RouteDefinition indexRoute( std::string path );

[[nodiscard]] http::RouteDefinition assetsRoute();

[[nodiscard]] http::RouteDefinition apiDocsFilesRoute();

[[nodiscard]] http::RouteDefinition pluginFilesRoute();

} // namespace idhan::httpapi
