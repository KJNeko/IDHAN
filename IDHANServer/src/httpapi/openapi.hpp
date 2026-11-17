#pragma once

#include "idhan/http/routing/ReflectedRoutes.hpp"

namespace idhan::httpapi
{

// clang-format off
[[= http::Get{"/openapi.json"}]]
[[= http::Group{"System"}]]
[[= http::Public{}]]
[[= http::Summary{"Read the compiled native API contract"}]]
http::HTTPTask< http::EncodedResult< http::JsonDocument > > openAPI();
// clang-format on

[[nodiscard]] std::string_view compiledOpenAPIDocument();
[[nodiscard]] http::RouteDefinition openAPIRoute();

} // namespace idhan::httpapi
