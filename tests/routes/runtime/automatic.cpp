#include <folly/coro/BlockingWait.h>

#include "gen_routes.hpp"
#include "idhan/http/openapi/OpenAPICompiler.hpp"
#include "idhan/http/routing/RouteRegistry.hpp"

#undef CHECK
#include <catch2/catch_test_macros.hpp>

namespace idhan::http::test
{

TEST_CASE( "Generated includes discover header-only endpoints in child namespaces", "[routes][generated]" )
{
	NoDependencies dependency;
	auto definitions { reflectedRoutes< httpapi::generated_routes::members >( dependency ) };
	REQUIRE( definitions.size() == 2 );
	RouteRegistry registry;
	for ( auto& definition : definitions ) registry.add( std::move( definition ) );

	CHECK( registry.match( proxygen::HTTPMethod::GET, "/automatic/alias" ).status == RouteMatch::Status::found );
	runtime::DrainRegistry work;
	proxygen::HTTPMessage message;
	message.setMethod( proxygen::HTTPMethod::GET );
	message.setURL( "/automatic/header" );
	RequestContext context {
		1, std::move( message ), {}, folly::IOBuf::create( 0 ), std::move( work.admit().value() )
	};
	auto response { folly::coro::blockingWait(
		registry.dispatch( registry.match( proxygen::HTTPMethod::GET, "/automatic/header" ), std::move( context ) ) ) };
	CHECK( response.message.getStatusCode() == 200 );
	CHECK(
		std::get< std::unique_ptr< folly::IOBuf > >( response.body )->to< std::string >()
	    == R"({"name":"discovered"})" );

	constexpr auto group { freezeOpenAPI< compileOpenAPIGroup< httpapi::generated_routes::members >().size() >(
		compileOpenAPIGroup< httpapi::generated_routes::members >() ) };
	const std::string_view records { group.data(), group.size() };
	CHECK( records.find( "/automatic/header" ) != std::string::npos );
	CHECK( records.find( "/automatic/alias" ) != std::string::npos );
}

} // namespace idhan::http::test
