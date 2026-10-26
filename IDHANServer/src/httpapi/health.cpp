#include "health.hpp"

#include <folly/io/IOBuf.h>

namespace idhan::httpapi
{

[[nodiscard]] static http::HTTPTask< http::Response > health( [[maybe_unused]] http::RequestContext context )
{
	constexpr std::string_view body { R"({"status":"ok"})" };
	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( body ) );
}

http::RouteDefinition healthRoute()
{
	return http::RouteDefinition { http::RouteMetadata { proxygen::HTTPMethod::GET, "/health", {}, {} }, health };
}

} // namespace idhan::httpapi
