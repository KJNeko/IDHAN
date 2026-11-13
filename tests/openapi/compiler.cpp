#include "idhan/http/openapi/OpenAPICompiler.hpp"

#undef CHECK
#include <catch2/catch_test_macros.hpp>

namespace idhan::http::test::outputContract
{

// clang-format off
[[= Output< ErrorResponse >{404}]]
HTTPTask< JsonResult< std::string > > typedSuccessWithError();

[[= Output< ErrorResponse >{501}]]
HTTPTask< Response > errorOnly();

[[= Output< std::string >{}]]
HTTPTask< Response > untypedSuccessAnnotation();

[[= Output< std::string >{}]]
HTTPTask< JsonResponse< std::string > > duplicateSuccessAnnotation();

[[= Output< void >{204}]]
HTTPTask< EmptyResponse< 204 > > emptySuccessAnnotation();
// clang-format on

HTTPTask< JsonResponse< std::string > > inferredOutput();

static_assert( hasValidOutputAnnotations< ^^typedSuccessWithError >() );
static_assert( hasValidOutputAnnotations< ^^errorOnly >() );
static_assert( !hasValidOutputAnnotations< ^^untypedSuccessAnnotation >() );
static_assert( !hasValidOutputAnnotations< ^^duplicateSuccessAnnotation >() );
static_assert( !hasValidOutputAnnotations< ^^emptySuccessAnnotation >() );
static_assert( hasValidOutputAnnotations< ^^inferredOutput >() );

} // namespace idhan::http::test::outputContract

namespace idhan::http::test::openapi
{

struct NodeInput
{
	std::string name;
	std::optional< int > offset;
	[[= Maximum { 1000 }]] int limit { 25 };
};

struct Node
{
	std::string name;
	std::optional< std::string > description;
	std::vector< Node > children;
};

struct QueryFields
{
	[[= Minimum { 1 }]][ [= Maximum { 4096 }] ] int domain;
	std::vector< int > tags;
};

using Result = std::variant< JsonResponse< Node, 201 >, EmptyResponse< 204 > >;

// clang-format off
[[= Post{"/nodes"}]]
[[= Summary{"Create a node"}]]
	// clang-format on
	static HTTPTask< Result >
		createNode( JsonBody< NodeInput >, Query< QueryFields > )
{
	co_return EmptyResponse< 204 > {};
}

// clang-format off
[[= Get{"/nodes"}]]
[[= Public{}]]
[[= Output<ErrorResponse>{404}]]
	// clang-format on
	static HTTPTask< JsonResult< Node > >
		nodes( RequestContext )
{
	co_return Response::error( 500, "Not called by the document compiler" );
}

// clang-format off
[[= Post{"/documents"}]]
[[= Input<>{}]]
static HTTPTask<JsonResponse<JsonDocument>> documents(RequestContext)
{
	co_return JsonResponse<JsonDocument>{{"{}"}};
}

[[= Get{"/query-only"}]]
[[= Parameters<QueryFields>{}]]
static HTTPTask<std::variant<EmptyResponse<204>, Response>> queryOnly(RequestContext)
{
	co_return Response::error(500, "Not called by the document compiler");
}

[[= Post{"/binary"}]]
[[= Input< RequestBody >{"application/octet-stream", false}]]
static HTTPTask<std::variant<EncodedResponse<std::string, "text/plain">, JsonResponse<JsonDocument>>> binary(RequestContext)
{
	co_return JsonResponse<JsonDocument>{{"{}"}};
}
[[= Get{"/validated/{id}/{uuid}"}]]
[[= Validation< validation::PositiveInteger<std::int16_t> >{"id"}]]
[[= Validation< validation::UUID >{"uuid"}]]
static HTTPTask<std::variant<EmptyResponse<204>, Response>> validated(RequestContext)
{
	co_return Response::error(500, "Not called by the document compiler");
}
// clang-format on

inline constexpr std::array< std::string_view, 1 > headers { "X-Test-Key" };
inline constexpr OpenAPIAuthentication authentication { headers, "key" };
constexpr auto members { std::meta::reflect_constant_array(
	std::meta::members_of( ^^idhan::http::test::openapi, std::meta::access_context::current() ) ) };
constexpr auto group { compiledOpenAPIGroup< members, authentication >() };
constexpr std::array fragments { std::string_view { group.data(), group.size() } };
constexpr auto document { freezeOpenAPI< compileOpenAPIDocument( fragments, "Test", "1", authentication ).size() >(
	compileOpenAPIDocument( fragments, "Test", "1", authentication ) ) };
static_assert( document.size() > 100 );

TEST_CASE( "OpenAPI is generated in constant evaluation from the route snapshot", "[openapi][compiletime]" )
{
	simdjson::dom::parser parser;
	simdjson::dom::element root;
	const std::string json { document.data(), document.size() };
	REQUIRE( parser.parse( json ).get( root ) == simdjson::SUCCESS );
	const auto paths { root[ "paths" ] };
	CHECK( paths[ "/nodes" ][ "post" ][ "summary" ].get_string().value() == "Create a node" );
	CHECK( paths[ "/nodes" ][ "post" ][ "security" ].get_array().value().size() == 2 );
	CHECK( paths[ "/nodes" ][ "get" ][ "security" ].get_array().value().size() == 0 );
	CHECK(
		paths[ "/nodes" ][ "post" ][ "responses" ][ "201" ][ "content" ][ "application/json" ].error()
	    == simdjson::SUCCESS );
	CHECK( paths[ "/nodes" ][ "post" ][ "responses" ][ "204" ][ "content" ].error() == simdjson::NO_SUCH_FIELD );
	CHECK( paths[ "/nodes" ][ "post" ][ "responses" ][ "401" ].error() == simdjson::SUCCESS );
	CHECK( paths[ "/nodes" ][ "get" ][ "responses" ][ "401" ].error() == simdjson::NO_SUCH_FIELD );
	CHECK(
		paths[ "/nodes" ][ "get" ][ "responses" ][ "200" ][ "content" ][ "application/json" ].error()
	    == simdjson::SUCCESS );
	CHECK(
		paths[ "/nodes" ][ "get" ][ "responses" ][ "404" ][ "content" ][ "application/json" ].error()
	    == simdjson::SUCCESS );
	CHECK(
		root[ "components" ][ "securitySchemes" ][ "api_key_header_0" ][ "name" ].get_string().value()
	    == "X-Test-Key" );
	constexpr auto input_name {
		std::define_static_string( openAPIComponentName< NodeInput, SchemaDirection::input >() )
	};
	constexpr auto output_name { std::define_static_string( openAPIComponentName< Node, SchemaDirection::output >() ) };
	const auto input { root[ "components" ][ "schemas" ][ input_name ] };
	CHECK( input[ "required" ].get_array().value().size() == 1 );
	CHECK( input[ "required" ].at( 0 ).get_string().value() == "name" );
	CHECK( input[ "properties" ][ "offset" ][ "anyOf" ].get_array().value().size() == 2 );
	const auto output { root[ "components" ][ "schemas" ][ output_name ] };
	CHECK( output[ "required" ].get_array().value().size() == 3 );
	CHECK(
		output[ "properties" ][ "children" ][ "items" ][ "$ref" ].get_string().value()
	    == "#/components/schemas/" + std::string { output_name } );
}

TEST_CASE(
	"Input and parameter annotations retain contracts alongside typed response returns",
	"[openapi][compiletime]" )
{
	simdjson::dom::parser parser;
	simdjson::dom::element root;
	const std::string json { document.data(), document.size() };
	REQUIRE( parser.parse( json ).get( root ) == simdjson::SUCCESS );
	const auto paths { root[ "paths" ] };
	const auto documents { paths[ "/documents" ][ "post" ] };
	CHECK( documents[ "requestBody" ][ "required" ].get_bool().value() );
	CHECK( documents[ "requestBody" ][ "content" ][ "application/json" ][ "schema" ].get_object().value().size() == 0 );
	CHECK(
		documents[ "responses" ][ "200" ][ "content" ][ "application/json" ][ "schema" ].get_object().value().size()
	    == 0 );
	CHECK( documents[ "responses" ][ "400" ][ "content" ][ "application/json" ].error() == simdjson::SUCCESS );
	CHECK( documents[ "parameters" ].get_array().value().size() == 0 );
	const auto query { paths[ "/query-only" ][ "get" ] };
	CHECK( query[ "requestBody" ].error() == simdjson::NO_SUCH_FIELD );
	CHECK( query[ "responses" ][ "204" ][ "content" ].error() == simdjson::NO_SUCH_FIELD );
	CHECK( query[ "parameters" ].get_array().value().size() == 2 );
	const auto binary { paths[ "/binary" ][ "post" ] };
	CHECK_FALSE( binary[ "requestBody" ][ "required" ].get_bool().value() );
	CHECK(
		binary[ "requestBody" ][ "content" ][ "application/octet-stream" ][ "schema" ][ "format" ].get_string().value()
	    == "binary" );
	CHECK( binary[ "responses" ][ "200" ][ "content" ].get_object().value().size() == 2 );
	CHECK(
		binary[ "responses" ][ "200" ][ "content" ][ "text/plain" ][ "schema" ][ "type" ].get_string().value()
	    == "string" );
	CHECK( binary[ "responses" ][ "200" ][ "content" ][ "application/json" ].error() == simdjson::SUCCESS );
}

TEST_CASE( "URL validation types supply the OpenAPI path constraints", "[openapi][validation]" )
{
	simdjson::dom::parser parser;
	simdjson::dom::element root;
	const std::string json { document.data(), document.size() };
	REQUIRE( parser.parse( json ).get( root ) == simdjson::SUCCESS );
	const auto parameters { root[ "paths" ][ "/validated/{id}/{uuid}" ][ "get" ][ "parameters" ].get_array().value() };
	REQUIRE( parameters.size() == 2 );
	for ( const auto parameter : parameters )
	{
		CHECK( parameter[ "in" ].get_string().value() == "path" );
		CHECK( parameter[ "required" ].get_bool().value() );
		const auto schema { parameter[ "schema" ] };
		if ( parameter[ "name" ].get_string().value() == "id" )
		{
			CHECK( schema[ "type" ].get_string().value() == "integer" );
			CHECK( schema[ "maximum" ].get_int64().value() == 32767 );
			CHECK( schema[ "minimum" ].get_int64().value() == 1 );
		}
		else
		{
			CHECK( parameter[ "name" ].get_string().value() == "uuid" );
			CHECK( schema[ "allOf" ].at( 0 )[ "type" ].get_string().value() == "string" );
			CHECK( schema[ "format" ].get_string().value() == "uuid" );
		}
	}
}

TEST_CASE( "Maximum annotations supply upper bounds for query and body values", "[openapi][bounds]" )
{
	simdjson::dom::parser parser;
	simdjson::dom::element root;
	const std::string json { document.data(), document.size() };
	REQUIRE( parser.parse( json ).get( root ) == simdjson::SUCCESS );
	const auto parameters { root[ "paths" ][ "/query-only" ][ "get" ][ "parameters" ].get_array().value() };
	REQUIRE( parameters.size() == 2 );
	bool domain_found {};
	for ( const auto parameter : parameters )
	{
		if ( parameter[ "name" ].get_string().value() != "domain" ) continue;

		domain_found = true;
		const auto schema { parameter[ "schema" ] };
		CHECK( schema[ "type" ].get_string().value() == "integer" );
		CHECK( schema[ "minimum" ].get_int64().value() == 1 );
		CHECK( schema[ "maximum" ].get_int64().value() == 4096 );
	}
	CHECK( domain_found );
	constexpr auto input_name {
		std::define_static_string( openAPIComponentName< NodeInput, SchemaDirection::input >() )
	};
	const auto input { root[ "components" ][ "schemas" ][ input_name ] };
	CHECK( input[ "properties" ][ "limit" ][ "type" ].get_string().value() == "integer" );
	CHECK( input[ "properties" ][ "limit" ][ "maximum" ].get_int64().value() == 1000 );
	CHECK( input[ "required" ].get_array().value().size() == 1 );
}

TEST_CASE( "OpenAPI quoting preserves text around escaped bytes", "[openapi][compiletime]" )
{
	CHECK( openAPIQuote( "" ) == "\"\"" );
	CHECK( openAPIQuote( "plain UTF-8: café" ) == "\"plain UTF-8: café\"" );
	CHECK( openAPIQuote( std::string_view { "a\"\\\n\0\x1f", 6 } ) == "\"a\\\"\\\\\\u000a\\u0000\\u001f\"" );
	CHECK( openAPIQuote( "before\tafter" ) == "\"before\\u0009after\"" );
}

TEST_CASE( "OpenAPI fragment fields preserve bytes and reject truncation", "[openapi][compiletime]" )
{
	std::string records;
	const std::string_view payload { "a\0b", 3 };
	appendOpenAPIRecordField( records, payload );
	appendOpenAPIRecordField( records, "" );
	std::size_t offset {};
	CHECK( nextOpenAPIRecordField( records, offset ) == payload );
	CHECK( nextOpenAPIRecordField( records, offset ).empty() );
	CHECK( offset == records.size() );
	CHECK_THROWS( nextOpenAPIRecordField( records, offset ) );

	offset = 0;
	CHECK_THROWS( nextOpenAPIRecordField( std::string_view { records }.substr( 0, 3 ), offset ) );
	offset = 0;
	CHECK_THROWS( nextOpenAPIRecordField( std::string_view { records }.substr( 0, 6 ), offset ) );
}

TEST_CASE( "Cached route fragments preserve the complete document", "[openapi][compiletime]" )
{
	constexpr auto route_fragments { compiledOpenAPIFragments< members, authentication >() };
	constexpr auto fragmented_document {
		freezeOpenAPI< compileOpenAPIDocument( route_fragments, "Test", "1", authentication ).size() >(
			compileOpenAPIDocument( route_fragments, "Test", "1", authentication ) )
	};
	CHECK( fragmented_document == document );
}

TEST_CASE( "OpenAPI compilation is deterministic", "[openapi][compiletime]" )
{
	constexpr auto duplicate { freezeOpenAPI< compileOpenAPIDocument( fragments, "Test", "1", authentication ).size() >(
		compileOpenAPIDocument( fragments, "Test", "1", authentication ) ) };
	CHECK( document == duplicate );
}

} // namespace idhan::http::test::openapi

namespace idhan::http::test::openapi
{

TEST_CASE( "JSON results default to status 200 and preserve errors and headers", "[openapi][response]" )
{
	JsonResponse< Node > success { { "root", {}, {} } };
	success.headers.set( "Cache-Control", "no-store" );
	JsonResult< Node > result { std::move( success ) };
	auto response { serializeResponse( std::move( result ) ) };
	CHECK( response.message.getStatusCode() == 200 );
	CHECK( response.message.getHeaders().getSingleOrEmpty( "Content-Type" ) == "application/json" );
	CHECK( response.message.getHeaders().getSingleOrEmpty( "Cache-Control" ) == "no-store" );

	const auto& body { std::get< std::unique_ptr< folly::IOBuf > >( response.body ) };
	simdjson::dom::parser parser;
	simdjson::dom::element parsed;
	REQUIRE( parser.parse( body->to< std::string >() ).get( parsed ) == simdjson::SUCCESS );
	CHECK( parsed[ "name" ].get_string().value() == "root" );
	CHECK( parsed[ "children" ].get_array().value().size() == 0 );

	JsonResult< Node, 201 > created { JsonResponse< Node, 201 > { { "created", {}, {} } } };
	CHECK( serializeResponse( std::move( created ) ).message.getStatusCode() == 201 );

	JsonResult< Node > error { Response::error( 404, "Missing node" ) };
	auto rejected { serializeResponse( std::move( error ) ) };
	CHECK( rejected.message.getStatusCode() == 404 );
	CHECK( rejected.message.getHeaders().getSingleOrEmpty( "Content-Type" ) == "application/json" );
}

TEST_CASE( "Encoded results preserve their owned transport and error responses", "[openapi][response]" )
{
	auto buffer { folly::IOBuf::copyBuffer( "payload" ) };
	const auto* data { buffer->data() };
	auto transport { Response::bytes( 200, std::move( buffer ), "image/webp" ) };
	transport.message.getHeaders().set( "Cache-Control", "no-store" );
	auto result { encodedResponse< RequestBody, "image/webp" >( std::move( transport ) ) };
	CHECK( std::holds_alternative< EncodedResponse< RequestBody, "image/webp" > >( result ) );
	auto response { serializeResponse( std::move( result ) ) };
	CHECK( response.message.getStatusCode() == 200 );
	CHECK( response.message.getHeaders().getSingleOrEmpty( "Content-Type" ) == "image/webp" );
	CHECK( response.message.getHeaders().getSingleOrEmpty( "Cache-Control" ) == "no-store" );
	CHECK( std::get< std::unique_ptr< folly::IOBuf > >( response.body )->data() == data );

	auto error { encodedResponse< RequestBody, "image/webp" >( Response::error( 503, "Storage unavailable" ) ) };
	CHECK( std::holds_alternative< Response >( error ) );
	CHECK( serializeResponse( std::move( error ) ).message.getStatusCode() == 503 );
}

} // namespace idhan::http::test::openapi
