#include <folly/coro/BlockingWait.h>

#include "idhan/http/routing/ReflectedRoutes.hpp"
#include "idhan/http/routing/RouteRegistry.hpp"
#include "httpapi/tags.hpp"

#undef CHECK
#include <catch2/catch_test_macros.hpp>

namespace idhan::http::test
{

struct CustomWireValue
{
	std::string decoded;
};

} // namespace idhan::http::test

namespace idhan::http
{

template <>
struct JsonCodec< test::CustomWireValue >
{
	static std::expected< test::CustomWireValue, ParseError > decode(
		const simdjson::dom::element element,
		std::string path )
	{
		std::string_view text;
		if ( element[ "wire" ].get_string().get( text ) != simdjson::SUCCESS )
			return std::unexpected( ParseError { std::move( path ), "wire must be a string" } );

		return test::CustomWireValue { std::string { text } };
	}

	static std::vector< JsonParameterObject > rules( std::vector< std::string > path )
	{
		return { { std::move( path ), { "wire" } } };
	}
};

} // namespace idhan::http

namespace idhan::http::test::reflected
{

struct Dependency
{
	int calls {};
};

struct Tag
{
	[[= Name { "namespace" }]] std::variant< int, std::string > tag_namespace;
	std::string subtag;
};

using TagInput = std::variant< int, std::string, Tag >;

struct Body
{
	[[= Maximum { 1000 }]] std::vector< int > records;
	std::optional< std::vector< TagInput > > tags;
	std::optional< std::vector< std::vector< TagInput > > > sets;
	[[= Maximum { 100 }]] std::int64_t depth {};
};

struct QueryFields
{
	[[= Minimum { 1 }]][ [= Maximum { 64 }] ] int domain;
	std::optional< bool > enabled;
};

struct PathFields
{
	[[= Minimum { 1 }]][ [= Maximum { 99 }] ] int id;
};

struct NestedBody
{
	Body request;
	std::map< std::string, JsonDocument > options;
};

struct DefaultFields
{
	int limit { 25 };
	std::optional< int > offset { 9 };
};

struct HeaderFields
{
	[[= Name { "x-count" }]] int count;

	[[= Name { "x-source" }]] std::vector< std::string > sources;
	std::optional< std::string > missing;
};

[[= Post { "/typed/{id}" }]] static HTTPTask< Response > typedHandler(
	Dependency& dependency,
	RequestContext context,
	Query< QueryFields > query,
	Path< PathFields > path,
	JsonBody< Body > body )
{
	CHECK( context.message().getPathAsStringPiece() == "/typed/42" );
	CHECK( query.value.domain == 7 );
	CHECK_FALSE( query.value.enabled.has_value() );
	CHECK( path.value.id == 42 );
	CHECK( body.value.records == std::vector< int > { 11 } );
	CHECK_FALSE( body.value.tags.has_value() );
	CHECK_FALSE( body.value.sets.has_value() );
	++dependency.calls;
	co_return Response::bytes( 201, folly::IOBuf::copyBuffer( "ok" ), "text/plain" );
}

// clang-format off
[[= Get{"/alias/a"}]]
[[= Get{"/alias/b"}]]
[[= Public {}]]
	// clang-format on
	static HTTPTask< Response >
		publicHandler( RequestContext )
{
	co_return Response::bytes( 200, folly::IOBuf::copyBuffer( "public" ), "text/plain" );
}

// clang-format off
[[= Post{"/custom"}]]
[[= Input< NestedBody > {}]]
[[= Parameters< QueryFields > {}]]
	// clang-format on
	static HTTPTask< Response >
		customHandler( RequestContext )
{
	co_return Response::bytes( 200, folly::IOBuf::copyBuffer( "custom" ), "text/plain" );
}

static std::vector< RouteDefinition > routes(
	Dependency& dependency,
	std::vector< RequestValidation > validations = {} )
{
	return reflectedRoutes< std::meta::reflect_constant_array(
		std::meta::members_of( ^^idhan::http::test::reflected, std::meta::access_context::current() ) ) >(
		dependency, std::move( validations ), { "idhan_key" } );
}

template < typename T >
static std::expected< T, ParseError > decodeText( std::string_view input )
{
	simdjson::dom::parser parser;
	simdjson::dom::element document;
	const simdjson::padded_string padded { input };
	const auto error { parser.parse( padded ).get( document ) };
	REQUIRE( error == simdjson::SUCCESS );
	return decodeJson< T >( document );
}

TEST_CASE( "Reflection discovers free functions, aliases and request schemas", "[parameters][reflection]" )
{
	Dependency dependency;
	auto definitions { routes( dependency ) };
	REQUIRE( definitions.size() == 4 );
	CHECK( definitions[ 0 ].metadata.path == "/typed/{id}" );
	CHECK(
		definitions[ 0 ].metadata.parameters->query
	    == std::vector< std::string > { "domain", "enabled", "idhan_key" } );
	REQUIRE( definitions[ 0 ].metadata.path_parameters.size() == 1 );
	CHECK( definitions[ 0 ].metadata.path_parameters[ 0 ].validate( "42" ).has_value() );
	CHECK_FALSE( definitions[ 0 ].metadata.path_parameters[ 0 ].validate( "0" ).has_value() );
	CHECK_FALSE( definitions[ 0 ].metadata.path_parameters[ 0 ].validate( "bad" ).has_value() );
	CHECK(
		definitions[ 3 ].metadata.parameters->query
	    == std::vector< std::string > { "domain", "enabled", "idhan_key" } );
	CHECK( definitions[ 3 ].metadata.parameters->json.size() == jsonParameterRules< NestedBody >().size() );
	RouteRegistry registry;
	for ( auto& definition : definitions ) registry.add( std::move( definition ) );

	CHECK( registry.match( proxygen::HTTPMethod::GET, "/alias/a" ).status == RouteMatch::Status::found );
	CHECK( registry.match( proxygen::HTTPMethod::GET, "/alias/b" ).status == RouteMatch::Status::found );
}

TEST_CASE( "Reflected optional JSON fields accept missing and null values", "[parameters][reflection][null]" )
{
	for ( const auto input : { R"({"records":[1]})", R"({"records":[1],"tags":null,"sets":null})" } )
	{
		auto body_e { decodeText< Body >( input ) };
		REQUIRE( body_e.has_value() );
		CHECK_FALSE( body_e.value().tags.has_value() );
		CHECK_FALSE( body_e.value().sets.has_value() );
	}
	CHECK_FALSE( decodeText< Body >( R"({"tags":null})" ).has_value() );
	CHECK_FALSE( decodeText< Body >( R"({"records":null})" ).has_value() );
	CHECK_FALSE( decodeText< Body >( R"({"records":[],"tags":false})" ).has_value() );
	CHECK_FALSE( decodeText< Body >( R"({"records":[2147483648]})" ).has_value() );
	CHECK_FALSE( decodeText< Body >( R"({"records":[1.5]})" ).has_value() );
}

TEST_CASE( "Nested reflected variants own values and generate unknown-field rules", "[parameters][reflection][json]" )
{
	const auto input {
		R"({"records":[1],"tags":[2,"artist:name",{"namespace":"artist","subtag":"name"}],"sets":[[{"namespace":3,"subtag":"name","typo":true}]],"extra":1})"
	};
	auto body_e { decodeText< Body >( input ) };
	REQUIRE( body_e.has_value() );
	const auto& body { body_e.value() };
	REQUIRE( body.tags.has_value() );
	CHECK( std::get< std::string >( body.tags.value()[ 1 ] ) == "artist:name" );
	CHECK( std::get< Tag >( body.tags.value()[ 2 ] ).subtag == "name" );
	simdjson::dom::parser parser;
	simdjson::dom::element document;
	const simdjson::padded_string padded { std::string_view { input } };
	REQUIRE( parser.parse( padded ).get( document ) == simdjson::SUCCESS );
	CHECK(
		unknownJsonParameters( document, jsonParameterRules< Body >() )
	    == std::vector< std::string > { "/extra", "/sets/0/0/typo" } );

	auto nested_e {
		decodeText< NestedBody >( R"({"request":{"records":[]},"options":{"custom":{"anything":[1,null]}}})" )
	};
	REQUIRE( nested_e.has_value() );
	CHECK( nested_e.value().options.at( "custom" ).text == R"({"anything":[1,null]})" );
}

TEST_CASE(
	"Reflected adapters decode before moving context and reject invalid requests",
	"[parameters][reflection][dispatch]" )
{
	Dependency dependency;
	RouteRegistry registry;
	for ( auto& route : routes( dependency ) )
	{
		route.metadata.parameters->unknown = UnknownParameterPolicy::reject;
		registry.add( std::move( route ) );
	}
	registry.seal();
	runtime::DrainRegistry work;
	for ( const auto body :
	      { R"({"records":[11],"tags":null})", R"({"records":null})", R"({"records":[11],"typo":1})" } )
	{
		proxygen::HTTPMessage message;
		message.setMethod( proxygen::HTTPMethod::POST );
		message.setURL( "/typed/42?domain=7" );
		auto parameters_e { RequestParameters::parseQuery( "domain=7" ) };
		REQUIRE( parameters_e.has_value() );
		parameters_e.value().append( "id", "42" );
		RequestContext context {
			1,
			std::move( message ),
			std::move( parameters_e.value() ),
			folly::IOBuf::copyBuffer( body ),
			std::move( work.admit().value() )
		};
		auto response { folly::coro::blockingWait(
			registry.dispatch( registry.match( proxygen::HTTPMethod::POST, "/typed/42" ), std::move( context ) ) ) };
		const bool valid { std::string_view { body } == R"({"records":[11],"tags":null})" };
		CHECK( response.message.getStatusCode() == ( valid ? 201 : 400 ) );
	}
	CHECK( dependency.calls == 1 );
}

TEST_CASE( "Public annotations preserve pre-body validation for protected routes", "[parameters][reflection][auth]" )
{
	Dependency dependency;
	RequestValidation reject {
		[]( const proxygen::HTTPMessage&, const RequestParameters& ) -> HTTPTask< std::optional< Response > >
		{ co_return Response::error( 401, "denied" ); }
	};
	RouteRegistry registry;
	for ( auto& route : routes( dependency, { reject } ) ) registry.add( std::move( route ) );

	proxygen::HTTPMessage message;
	message.setMethod( proxygen::HTTPMethod::POST );
	message.setURL( "/typed/42" );
	auto rejection { folly::coro::blockingWait(
		registry.validateRequest( registry.match( proxygen::HTTPMethod::POST, "/typed/42" ), message, {} ) ) };
	REQUIRE( rejection.has_value() );
	CHECK( rejection->message.getStatusCode() == 401 );
	CHECK( dependency.calls == 0 );
	CHECK_FALSE(
		folly::coro::blockingWait(
			registry.validateRequest( registry.match( proxygen::HTTPMethod::GET, "/alias/a" ), message, {} ) )
			.has_value() );
}

TEST_CASE(
	"Reflected defaults apply only to absent fields and null optionals reset",
	"[parameters][reflection][defaults]" )
{
	auto defaults_e { decodeText< DefaultFields >( R"({})" ) };
	REQUIRE( defaults_e.has_value() );
	CHECK( defaults_e.value().limit == 25 );
	CHECK_FALSE( defaults_e.value().offset.has_value() );
	CHECK_FALSE( decodeText< DefaultFields >( R"({"limit":null})" ).has_value() );
	auto null_e { decodeText< DefaultFields >( R"({"offset":null})" ) };
	REQUIRE( null_e.has_value() );
	CHECK_FALSE( null_e.value().offset.has_value() );
	auto query_e { decodeParameters< DefaultFields >( {} ) };
	REQUIRE( query_e.has_value() );
	CHECK( query_e.value().limit == 25 );
	CHECK_FALSE( query_e.value().offset.has_value() );
}

TEST_CASE( "Maximum annotations bound path, query and JSON values", "[parameters][reflection][bounds]" )
{
	Dependency dependency;
	const auto definitions { routes( dependency ) };
	const auto& validate_id { definitions[ 0 ].metadata.path_parameters[ 0 ].validate };
	CHECK( validate_id( "99" ).has_value() );
	auto path_e { validate_id( "100" ) };
	REQUIRE_FALSE( path_e.has_value() );
	CHECK( path_e.error().field == "id" );
	CHECK( path_e.error().message == "must be at most 99" );

	for ( const auto query : { "domain=64", "domain=65" } )
	{
		auto parameters_e { RequestParameters::parseQuery( query ) };
		REQUIRE( parameters_e.has_value() );
		auto query_e { decodeParameters< QueryFields >( parameters_e.value() ) };
		const bool within_bound { std::string_view { query } == "domain=64" };
		REQUIRE( query_e.has_value() == within_bound );
		if ( within_bound ) continue;

		CHECK( query_e.error().field == "domain" );
		CHECK( query_e.error().message == "domain must be at most 64" );
	}

	CHECK( decodeText< Body >( R"({"records":[1000],"depth":100})" ).has_value() );
	CHECK( decodeText< Body >( R"({"records":[]})" ).has_value() );
	CHECK_FALSE( decodeText< Body >( R"({"records":[1001]})" ).has_value() );
	CHECK_FALSE( decodeText< Body >( R"({"records":[1],"depth":101})" ).has_value() );
}

TEST_CASE(
	"Reflected header and raw-body arguments preserve repeated values and bytes",
	"[parameters][reflection][raw]" )
{
	runtime::DrainRegistry work;
	proxygen::HTTPMessage message;
	message.setMethod( proxygen::HTTPMethod::POST );
	message.setURL( "/raw" );
	message.getHeaders().add( "X-Count", "3" );
	message.getHeaders().add( "X-Source", "first" );
	message.getHeaders().add( "X-Source", "second" );
	const std::string bytes { "a\0b", 3 };
	RequestContext context {
		1, std::move( message ), {}, folly::IOBuf::copyBuffer( bytes ), std::move( work.admit().value() )
	};
	auto headers_e { decodeRequestArgument< Headers< HeaderFields > >( context ) };
	REQUIRE( headers_e.has_value() );
	CHECK( headers_e.value().value.count == 3 );
	CHECK( headers_e.value().value.sources == std::vector< std::string > { "first", "second" } );
	CHECK_FALSE( headers_e.value().value.missing.has_value() );
	auto body_e { decodeRequestArgument< RequestBody >( context ) };
	REQUIRE( body_e.has_value() );
	CHECK( std::get< std::unique_ptr< folly::IOBuf > >( body_e.value() )->to< std::string >() == bytes );
}

TEST_CASE( "Custom JSON codecs control decoding and wire field declarations", "[parameters][reflection][codec]" )
{
	auto value_e { decodeText< CustomWireValue >( R"({"wire":"owned value"})" ) };
	REQUIRE( value_e.has_value() );
	CHECK( value_e.value().decoded == "owned value" );
	CHECK_FALSE( decodeText< CustomWireValue >( R"({"decoded":"wrong wire name"})" ).has_value() );
	const auto rules { jsonParameterRules< CustomWireValue >() };
	REQUIRE( rules.size() == 1 );
	CHECK( rules[ 0 ].names == std::vector< std::string > { "wire" } );
}

} // namespace idhan::http::test::reflected

namespace idhan::http::test::path_validation
{

struct Dependency
{
	int calls {};
};

// clang-format off
[[= Get{"/validated/{record_id}/{layout_id}"}]]
[[= Validation< validation::PositiveInteger<std::int16_t> >{"record_id"}]]
[[= Validation< validation::UUID >{"layout_id"}]]
static HTTPTask<Response> handler(Dependency& dependency, RequestContext)
{
	++dependency.calls;
	co_return Response::json(200, folly::IOBuf::copyBuffer("{}"));
}
// clang-format on

TEST_CASE( "Premade URL validators check complete values and storage ranges", "[parameters][validation]" )
{
	using PositiveID = validation::PositiveInteger< std::int16_t >;
	for ( const auto value : { "1", "32767" } ) CHECK( PositiveID::validate( "id", value ).has_value() );

	for ( const auto value : { "", "0", "-1", "+1", " 1", "1x", "32768" } )
	{
		auto result_e { PositiveID::validate( "record_id", value ) };
		REQUIRE_FALSE( result_e.has_value() );
		CHECK( result_e.error().field == "record_id" );
	}
	CHECK( validation::Integer< std::int16_t >::validate( "id", "-32768" ).has_value() );
	CHECK( validation::Integer< std::int16_t >::validate( "id", "0" ).has_value() );
	CHECK_FALSE( validation::Integer< std::int16_t >::validate( "id", "-32769" ).has_value() );
	CHECK( validation::Integer< std::uint64_t >::validate( "id", "0" ).has_value() );
	CHECK( validation::Integer< std::uint64_t >::validate( "id", "18446744073709551615" ).has_value() );
	CHECK_FALSE( validation::Integer< std::uint64_t >::validate( "id", "18446744073709551616" ).has_value() );
	CHECK_FALSE( validation::Integer< std::uint64_t >::validate( "id", "-1" ).has_value() );
	CHECK( validation::UUID::validate( "id", "12345678-abcd-ABCD-0000-123456789abc" ).has_value() );
	for ( const auto value :
	      { "",
	        "12345678-abcd-abcd-0000-123456789abz",
	        "12345678_abcd-abcd-0000-123456789abc",
	        "12345678-abcd-abcd-0000-123456789abc0" } )
		CHECK_FALSE( validation::UUID::validate( "id", value ).has_value() );
}

TEST_CASE( "URL validation rejects bad parameters before invoking a handler", "[parameters][validation][dispatch]" )
{
	Dependency dependency;
	auto definitions { reflectedRoutes< std::meta::reflect_constant_array( std::array { ^^handler } ) >( dependency ) };
	RouteRegistry registry;
	for ( auto& definition : definitions ) registry.add( std::move( definition ) );

	registry.seal();
	runtime::DrainRegistry work;
	for ( const auto path :
	      { "/validated/1/12345678-abcd-abcd-0000-123456789abc",
	        "/validated/0/12345678-abcd-abcd-0000-123456789abc",
	        "/validated/32768/12345678-abcd-abcd-0000-123456789abc",
	        "/validated/1/not-a-uuid" } )
	{
		proxygen::HTTPMessage message;
		message.setMethod( proxygen::HTTPMethod::GET );
		message.setURL( path );
		RequestContext context {
			1, std::move( message ), {}, folly::IOBuf::create( 0 ), std::move( work.admit().value() )
		};
		auto response { folly::coro::blockingWait(
			registry.dispatch( registry.match( proxygen::HTTPMethod::GET, path ), std::move( context ) ) ) };
		const bool valid { std::string_view { path }.starts_with( "/validated/1/12345678" ) };
		CHECK( response.message.getStatusCode() == ( valid ? 200 : 400 ) );
	}
	CHECK( dependency.calls == 1 );
}

TEST_CASE( "URL validation must name a placeholder in the route", "[parameters][validation]" )
{
	RouteMetadata metadata { proxygen::HTTPMethod::GET, "/{id}", reflectedPathParameters( "/{id}" ), {} };
	NoDependencies dependency;
	const auto missing { Validation< validation::Integer<> > { "missing" } };
	CHECK_THROWS_AS( missing.configure( metadata, dependency ), std::invalid_argument );
}

} // namespace idhan::http::test::path_validation

namespace idhan::http::test
{
TEST_CASE( "Autocomplete query accepts the documented display enum values", "[parameters][reflection][autocomplete]" )
{
	for ( const auto mode : { "display", "storage" } )
	{
		const auto parameters_e { RequestParameters::parseQuery(
			std::string { "tag=sample&limit=100&include_unused=true&tag_display_type=" } + mode ) };
		REQUIRE( parameters_e.has_value() );
		const auto query_e { decodeParameters< httpapi::AutocompleteQuery >( parameters_e.value() ) };
		INFO( mode );

		REQUIRE( query_e.has_value() );
		const auto& query { query_e.value() };
		CHECK( query.tag == "sample" );
		CHECK( query.limit == 100 );
		CHECK( query.include_unused );
		CHECK(
			query.tag_display_type
		    == ( std::string_view { mode } == "display" ?
		             httpapi::TagCountDisplayType::display :
		             httpapi::TagCountDisplayType::storage ) );
	}
}

TEST_CASE( "Autocomplete enum defaults and validation remain strict", "[parameters][reflection][autocomplete]" )
{
	const auto parameters_e { RequestParameters::parseQuery( "tag=sample" ) };
	REQUIRE( parameters_e.has_value() );
	const auto query_e { decodeParameters< httpapi::AutocompleteQuery >( parameters_e.value() ) };
	REQUIRE( query_e.has_value() );
	CHECK( query_e.value().tag_display_type == httpapi::TagCountDisplayType::display );
	for ( const auto mode : { "unknown", "DISPLAY", "0", "" } )
	{
		const auto invalid_parameters_e {
			RequestParameters::parseQuery( std::string { "tag=sample&tag_display_type=" } + mode )
		};
		REQUIRE( invalid_parameters_e.has_value() );
		const auto invalid_e { decodeParameters< httpapi::AutocompleteQuery >( invalid_parameters_e.value() ) };
		REQUIRE_FALSE( invalid_e.has_value() );
		CHECK( invalid_e.error().field == "tag_display_type" );
		CHECK( invalid_e.error().status == 400 );
		CHECK( invalid_e.error().message == "must be one of: storage, display" );
	}
}
} // namespace idhan::http::test
