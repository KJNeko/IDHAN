#include <folly/coro/BlockingWait.h>

#include "idhan/http/routing/ReflectedRoutes.hpp"
#include "idhan/http/routing/RouteRegistry.hpp"

#undef CHECK
#include <catch2/catch_test_macros.hpp>

namespace idhan::http::test::responses
{

enum class State
{
	ready,
	pending
};

struct Item
{
	int id;
	[[= Name { "namespace" }]] std::string tag_namespace;
	std::optional< std::string > description;
	std::vector< bool > flags;
	State state;
};

struct Collection
{
	std::vector< Item > items;
	std::map< std::string, std::variant< int, std::string > > properties;
};

using Result = std::variant< JsonResponse< Item, 201 >, EmptyResponse< 204 >, Response >;

struct QueryFields
{
	int mode;
};

[[= Get { "/typed-response" }]] static HTTPTask< Result > typedResponse( Query< QueryFields > query )
{
	if ( query.value.mode == 1 ) co_return EmptyResponse< 204 > {};

	if ( query.value.mode == 2 ) co_return Response::error( 404, "missing" );

	JsonResponse< Item, 201 > response { { 3, "artist", std::nullopt, { true, false }, State::ready } };
	response.headers.add( "Location", "/items/3" );
	response.headers.add( "Set-Cookie", "a=1" );
	response.headers.add( "Set-Cookie", "b=2" );
	co_return response;
}

TEST_CASE( "Reflected responses serialize nested owned values using wire names", "[parameters][reflection][response]" )
{
	Collection value { { { 3, "artist", std::nullopt, { true, false }, State::ready } },
	                   { { "number", 4 }, { "text", std::string { "quote\"\n" } } } };
	auto json_e { serializeJson( value ) };
	REQUIRE( json_e.has_value() );
	simdjson::dom::parser parser;
	simdjson::dom::element document;
	REQUIRE( parser.parse( json_e.value() ).get( document ) == simdjson::SUCCESS );
	CHECK( document[ "items" ].at( 0 )[ "namespace" ].get_string().value() == "artist" );
	CHECK( document[ "items" ].at( 0 )[ "description" ].is_null() );
	CHECK( document[ "items" ].at( 0 )[ "state" ].get_string().value() == "ready" );
	CHECK( document[ "items" ].at( 0 )[ "flags" ].at( 0 ).get_bool().value() );
	CHECK_FALSE( document[ "items" ].at( 0 )[ "flags" ].at( 1 ).get_bool().value() );
	CHECK( document[ "properties" ][ "number" ].get_int64().value() == 4 );
	CHECK( document[ "properties" ][ "text" ].get_string().value() == "quote\"\n" );
}

TEST_CASE( "Response serialization rejects invalid numbers, enums and raw JSON", "[parameters][reflection][response]" )
{
	CHECK_FALSE( serializeJson( std::numeric_limits< double >::infinity() ).has_value() );
	CHECK_FALSE( serializeJson( std::numeric_limits< double >::quiet_NaN() ).has_value() );
	CHECK_FALSE( serializeJson( static_cast< State >( 99 ) ).has_value() );
	CHECK_FALSE( serializeJson( JsonDocument { "{" } ).has_value() );
	CHECK_FALSE( serializeJson( std::string { "\xff", 1 } ).has_value() );
	auto document_e { serializeJson( JsonDocument { R"({"custom":[null,1]})" } ) };
	REQUIRE( document_e.has_value() );
	CHECK( document_e.value() == R"({"custom":[null,1]})" );
	auto response { serializeResponse( JsonResponse< double > { std::numeric_limits< double >::infinity() } ) };
	CHECK( response.message.getStatusCode() == 500 );
}

TEST_CASE(
	"Annotated handlers return typed response variants with status and headers",
	"[parameters][reflection][response]" )
{
	NoDependencies dependency;
	auto routes { reflectedRoutes< std::meta::reflect_constant_array(
		std::meta::members_of( ^^idhan::http::test::responses, std::meta::access_context::current() ) ) >(
		dependency ) };
	REQUIRE( routes.size() == 1 );
	RouteRegistry registry;
	registry.add( std::move( routes[ 0 ] ) );
	registry.seal();
	runtime::DrainRegistry work;
	for ( const auto mode : { 0, 1, 2 } )
	{
		proxygen::HTTPMessage message;
		message.setMethod( proxygen::HTTPMethod::GET );
		message.setURL( "/typed-response?mode=" + std::to_string( mode ) );
		RequestContext context {
			1, std::move( message ), {}, folly::IOBuf::create( 0 ), std::move( work.admit().value() )
		};
		auto response { folly::coro::blockingWait( registry.dispatch(
			registry.match( proxygen::HTTPMethod::GET, "/typed-response" ), std::move( context ) ) ) };
		if ( mode == 0 )
		{
			CHECK( response.message.getStatusCode() == 201 );
			CHECK( response.message.getHeaders().getSingleOrEmpty( "Location" ) == "/items/3" );
			CHECK( response.message.getHeaders().getNumberOfValues( "Set-Cookie" ) == 2 );
			CHECK( response.message.getHeaders().getSingleOrEmpty( "Content-Type" ) == "application/json" );
			CHECK(
				std::get< std::unique_ptr< folly::IOBuf > >( response.body )->to< std::string >().find( "artist" )
			    != std::string::npos );
		}
		else if ( mode == 1 )
		{
			CHECK( response.message.getStatusCode() == 204 );
			CHECK( std::holds_alternative< std::monostate >( response.body ) );
		}
		else
		{
			CHECK( response.message.getStatusCode() == 404 );
			CHECK( response.message.getHeaders().getSingleOrEmpty( "Content-Type" ) == "application/json" );
		}
	}
}

} // namespace idhan::http::test::responses
