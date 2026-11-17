#include "openapi.hpp"

#include "apiAuth.hpp"
#include "apiVersion.hpp"
#include "gen_routes.hpp"
#include "idhan/http/openapi/OpenAPICompiler.hpp"

namespace idhan::httpapi
{

http::HTTPTask< http::EncodedResult< http::JsonDocument > > openAPI()
{
	const auto document { compiledOpenAPIDocument() };
	// Static storage outlives every response; only an IOBuf wrapper is allocated.
	auto response { http::Response::json( 200, folly::IOBuf::wrapBuffer( document.data(), document.size() ) ) };
	response.message.getHeaders().set( "Cache-Control", "no-cache" );
	co_return http::encodedResponse< http::JsonDocument >( std::move( response ) );
}

static constexpr auto nativeOpenAPIFragments {
	http::compiledOpenAPIFragments< generated_routes::members, nativeOpenAPIAuthentication >()
};

consteval std::string compileNativeOpenAPI()
{
	const auto version { http::openAPINumber( IDHAN_API_MAJOR ) + "." + http::openAPINumber( IDHAN_API_MINOR ) + "."
		                 + http::openAPINumber( IDHAN_API_PATCH ) };
	return http::compileOpenAPIDocument( nativeOpenAPIFragments, "IDHAN API", version, nativeOpenAPIAuthentication );
}

//! Materialize once: sizing the final section must not encode the document again.
static constexpr auto nativeOpenAPIBytes { std::define_static_array( compileNativeOpenAPI() ) };

[[gnu::used, gnu::section( ".idhan.openapi.document" ), gnu::aligned( 1 )]]
static constexpr auto openAPIDocument {
	http::freezeOpenAPI< nativeOpenAPIBytes.size() >( { nativeOpenAPIBytes.data(), nativeOpenAPIBytes.size() } )
};

std::string_view compiledOpenAPIDocument()
{
	return { openAPIDocument.data(), openAPIDocument.size() };
}

http::RouteDefinition openAPIRoute()
{
	static http::NoDependencies dependency;
	auto routes { http::reflectedRoutes<
		std::meta::reflect_constant_array( std::array< std::meta::info, 1 > { { ^^openAPI } } ) >( dependency ) };
	return std::move( routes.front() );
}

} // namespace idhan::httpapi
