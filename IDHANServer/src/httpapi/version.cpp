#include "version.hpp"

#include <simdjson.h>

#include <folly/io/IOBuf.h>

#include <stdexcept>

#include "apiVersion.hpp"
#include "idhan/versions.hpp"
#include "logging/format_ns.hpp"

namespace idhan::httpapi
{

[[nodiscard]] static http::HTTPTask< http::Response > version( [[maybe_unused]] http::RequestContext context )
{
	auto version_str { format_ns::format( "{}.{}.{}", IDHAN_MAJOR_VERSION, IDHAN_MINOR_VERSION, IDHAN_PATCH_VERSION ) };
#if defined( FGL_GIT_UNSYNCED ) && FGL_GIT_UNSYNCED == 1
	version_str += "-tainted";
#endif

	const auto api_version_str {
		format_ns::format( "{}.{}.{}", IDHAN_API_MAJOR, IDHAN_API_MINOR, IDHAN_API_PATCH )
	};

	simdjson::builder::string_builder builder;
	builder.start_object();

	builder.escape_and_append_with_quotes( "idhan_server_version" );
	builder.append_colon();
	builder.start_object();
	builder.append_key_value( "string", version_str );
	builder.append_comma();
	builder.append_key_value( "major", IDHAN_MAJOR_VERSION );
	builder.append_comma();
	builder.append_key_value( "minor", IDHAN_MINOR_VERSION );
	builder.append_comma();
	builder.append_key_value( "patch", IDHAN_PATCH_VERSION );
	builder.end_object();
	builder.append_comma();

	builder.escape_and_append_with_quotes( "idhan_api_version" );
	builder.append_colon();
	builder.start_object();
	builder.append_key_value( "string", api_version_str );
	builder.append_comma();
	builder.append_key_value( "major", IDHAN_API_MAJOR );
	builder.append_comma();
	builder.append_key_value( "minor", IDHAN_API_MINOR );
	builder.append_comma();
	builder.append_key_value( "patch", IDHAN_API_PATCH );
	builder.end_object();
	builder.append_comma();

	builder.append_key_value( "branch", FGL_GIT_BRANCH );
	builder.append_comma();
	builder.append_key_value( "commit", FGL_GIT_COMMIT );
	builder.append_comma();
	builder.append_key_value( "tag", FGL_GIT_TAG );
	builder.append_comma();
	builder.append_key_value( "build", FGL_BUILD_TYPE );
	builder.append_comma();
	builder.append_key_value( "unsynced", FGL_GIT_UNSYNCED );
	builder.append_comma();
	builder.append_key_value( "build_on", IDHAN_BUILD_DATE ", " IDHAN_BUILD_TIME );

	builder.end_object();

	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( *json ) );
}

http::RouteDefinition versionRoute()
{
	return http::RouteDefinition { http::RouteMetadata { proxygen::HTTPMethod::GET, "/version", {}, {} }, version };
}

} // namespace idhan::httpapi
