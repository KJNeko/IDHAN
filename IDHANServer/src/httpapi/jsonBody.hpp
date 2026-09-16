#pragma once

#include <expected>
#include <string>

#include "idhan/http/RequestContext.hpp"
#include "idhan/http/Response.hpp"

namespace idhan::httpapi
{

[[nodiscard]] std::expected< std::string, http::Response > readValidatedJsonBody( http::RequestContext& context );

}
