#pragma once

#include "idhan/http/RouteSpec.hpp"

namespace idhan::httpapi
{

// Native endpoints must declare their consumed names before registry sealing.
// Every declaration currently uses warn mode; rejection remains an explicit policy choice.
[[nodiscard]] http::RouteDefinition declareEndpointParameters( http::RouteDefinition route );

} // namespace idhan::httpapi
