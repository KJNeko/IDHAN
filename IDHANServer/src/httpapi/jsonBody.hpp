#pragma once

#include <expected>
#include <string>

#include "idhan/http/request/RequestContext.hpp"
#include "idhan/http/response/Response.hpp"

namespace idhan::httpapi
{

[[nodiscard]] std::expected< std::string, http::Response > readRequestBody( http::RequestContext& context );

[[nodiscard]] std::expected< std::string, http::Response > readValidatedJsonBody( http::RequestContext& context );

}
