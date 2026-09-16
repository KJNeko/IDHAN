#include <catch2/catch_test_macros.hpp>
#include <folly/ScopeGuard.h>
#include <folly/coro/Baton.h>

#include <atomic>
#include <functional>
#include <future>

#include "CurlHelper.hpp"
#include "idhan/http/HTTPConfig.hpp"
#include "idhan/http/HTTPServer.hpp"
#include "idhan/http/RouteRegistry.hpp"

TEST_CASE( "HTTPServer constructs", "[http]" )
{
	auto config { idhan::http::HTTPConfig::defaults() };
	config.port = 0;
	config.tls = false;
	config.ipv6_listen.clear();
	idhan::http::RouteRegistry routes;
	routes.seal();
	idhan::http::HTTPServer server { std::move( config ), std::move( routes ) };
	REQUIRE( server.port() == 0 );
}

using namespace idhan::http;

struct ValidationFixture
{
	folly::coro::Baton release {};
	std::promise< void > entered {};
	std::atomic_int second_calls {}, parser_calls {}, handler_calls {};

	HTTPTask< std::optional< Response > > validate( const proxygen::HTTPMessage& request, const RequestParameters& )
	{
		entered.set_value();
		co_await release;
		const auto& key { request.getHeaders().getSingleOrEmpty( "Authorization" ) };
		if ( key == "throw" ) throw std::runtime_error( "secret validation failure" );

		if ( key != "accepted" ) co_return Response::error( 401, "Unauthorized" );

		co_return std::nullopt;
	}

	HTTPTask< std::optional< Response > > second( const proxygen::HTTPMessage&, const RequestParameters& )
	{
		++second_calls;
		co_return std::nullopt;
	}

	std::expected< std::string, ParseError > parse( const RequestContext& context )
	{
		++parser_calls;
		return std::get< std::unique_ptr< folly::IOBuf > >( context.body() )->to< std::string >();
	}

	HTTPTask< Response > handle( RequestContext, std::string body )
	{
		++handler_calls;
		co_return Response::bytes( 200, folly::IOBuf::copyBuffer( body ), "text/plain" );
	}

	RouteRegistry routes()
	{
		RouteRegistry routes;
		routes.add( makeRoute(
			RouteSpec< std::string > {
				{ proxygen::HTTPMethod::POST,
		          "/validated",
		          {},
		          { std::bind_front( &ValidationFixture::validate, this ),
		            std::bind_front( &ValidationFixture::second, this ) } },
				std::bind_front( &ValidationFixture::parse, this ),
				std::bind_front( &ValidationFixture::handle, this ) } ) );
		routes.seal();
		return routes;
	}
};

TEST_CASE( "Request validation completes before body acceptance", "[http][validation][integration]" )
{
	ValidationFixture fixture;
	auto entered { fixture.entered.get_future() };
	auto config { HTTPConfig::defaults() };
	config.port = 0;
	config.tls = false;
	config.ipv6_listen.clear();
	config.io_threads = 1;
	HTTPServer server { config, fixture.routes() };
	server.start();
	std::string authorization;
	int expected_status { 401 };
	SECTION( "Rejected client sends the body without waiting for validation" )
	{}
	SECTION( "Accepted request processes its already-sent body" )
	{
		authorization = "accepted";
		expected_status = 200;
	}
	SECTION( "Validation exceptions reject before the body" )
	{
		authorization = "throw";
		expected_status = 500;
	}

	idhan::test::CurlRequest request;
	request.url = idhan::test::localUrl( server.port(), "/validated" );
	request.method = "POST";
	request.body = "echo";
	request.timeout = std::chrono::seconds( 3 );
	// Keep curl's fallback upload timeout beyond the test's overall deadline.
	request.expect_continue_timeout = std::chrono::seconds( 5 );
	std::string authorization_header { "Authorization: " };
	authorization_header += authorization;
	request.headers.push_back( std::move( authorization_header ) );

	auto pending { std::async( std::launch::async, idhan::test::send, request ) };
	const auto unblock { folly::makeGuard( [ &fixture ] { fixture.release.post(); } ) };
	REQUIRE( entered.wait_for( std::chrono::seconds( 3 ) ) == std::future_status::ready );
	REQUIRE( pending.wait_for( std::chrono::milliseconds( 50 ) ) == std::future_status::timeout );
	REQUIRE( fixture.second_calls == 0 );
	REQUIRE( fixture.parser_calls == 0 );
	REQUIRE( fixture.handler_calls == 0 );
	fixture.release.post();
	const auto response { pending.get() };
	REQUIRE( response.status == expected_status );

	if ( expected_status == 200 )
	{
		REQUIRE( response.informational_statuses.empty() );
		REQUIRE( response.uploaded_bytes == request.body.size() );
		REQUIRE( response.body == request.body );
	}
	else
	{
		REQUIRE( response.informational_statuses.empty() );
		REQUIRE( response.uploaded_bytes == request.body.size() );
		REQUIRE( response.header( "Connection" ) == "close" );
		REQUIRE( response.body.find( "secret" ) == std::string::npos );
	}
	REQUIRE( fixture.parser_calls == ( expected_status == 200 ? 1 : 0 ) );
	REQUIRE( fixture.handler_calls == ( expected_status == 200 ? 1 : 0 ) );
	REQUIRE( fixture.second_calls == ( expected_status == 200 ? 1 : 0 ) );
}
