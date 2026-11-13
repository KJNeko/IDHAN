#include <folly/coro/BlockingWait.h>

#include "httpapi/fileMetadata.hpp"
#include "httpapi/openapi.hpp"
#include "idhan/http/openapi/OpenAPICompiler.hpp"

#undef CHECK
#include <catch2/catch_test_macros.hpp>

namespace idhan::http::test
{

TEST_CASE( "The OpenAPI endpoint serves the precompiled bytes without a database", "[openapi][endpoint]" )
{
	auto route { httpapi::openAPIRoute() };
	CHECK( route.metadata.path == "/openapi.json" );
	CHECK( route.metadata.method == proxygen::HTTPMethod::GET );
	CHECK( route.metadata.before_body.empty() );
	runtime::DrainRegistry work;
	proxygen::HTTPMessage message;
	message.setMethod( proxygen::HTTPMethod::GET );
	message.setURL( "/openapi.json" );
	RequestContext context {
		1, std::move( message ), {}, folly::IOBuf::create( 0 ), std::move( work.admit().value() )
	};
	auto response { folly::coro::blockingWait( route.invoke( std::move( context ) ) ) };
	CHECK( response.message.getStatusCode() == 200 );
	CHECK( response.message.getHeaders().getSingleOrEmpty( "Content-Type" ) == "application/json" );
	const auto compiled { httpapi::compiledOpenAPIDocument() };
	const auto& buffer { std::get< std::unique_ptr< folly::IOBuf > >( response.body ) };
	CHECK( buffer->length() == compiled.size() );
	CHECK( reinterpret_cast< const char* >( buffer->data() ) == compiled.data() );
	simdjson::dom::parser parser;
	simdjson::dom::element document;
	const std::string text { compiled };
	REQUIRE( parser.parse( text ).get( document ) == simdjson::SUCCESS );
	CHECK( document[ "paths" ][ "/openapi.json" ][ "get" ][ "security" ].get_array().value().size() == 0 );
	CHECK( document[ "paths" ][ "/health" ][ "get" ][ "responses" ][ "200" ].error() == simdjson::SUCCESS );
	CHECK( document[ "paths" ][ "/records/tags/add" ][ "post" ][ "requestBody" ].error() == simdjson::SUCCESS );
}

TEST_CASE( "Native response declarations use their wire media types and statuses", "[openapi][endpoint]" )
{
	simdjson::dom::parser parser;
	simdjson::dom::element document;
	const std::string text { httpapi::compiledOpenAPIDocument() };
	REQUIRE( parser.parse( text ).get( document ) == simdjson::SUCCESS );
	const auto paths { document[ "paths" ] };
	const auto duplicate { paths[ "/relationships/duplicates/add" ][ "post" ] };
	CHECK( duplicate[ "responses" ][ "200" ][ "content" ][ "application/json" ].error() == simdjson::SUCCESS );
	CHECK( duplicate[ "responses" ][ "200" ][ "content" ][ "text/plain" ].error() == simdjson::NO_SUCH_FIELD );
	CHECK( duplicate[ "responses" ][ "400" ][ "content" ][ "application/json" ].error() == simdjson::SUCCESS );
	const auto thumbnail { paths[ "/records/{record_id}/thumbnail" ][ "get" ] };
	CHECK( thumbnail[ "responses" ][ "200" ][ "content" ][ "image/webp" ].error() == simdjson::SUCCESS );
	CHECK( thumbnail[ "requestBody" ].error() == simdjson::NO_SUCH_FIELD );
	const auto file { paths[ "/file/import" ][ "post" ] };
	CHECK(
		file[ "requestBody" ][ "content" ][ "application/octet-stream" ][ "schema" ][ "format" ].get_string().value()
	    == "binary" );
	CHECK( file[ "responses" ][ "200" ][ "content" ][ "application/json" ].error() == simdjson::SUCCESS );
}

TEST_CASE(
	"Duplicate relationship schemas expose integer record IDs in both request forms",
	"[openapi][relationships]" )
{
	simdjson::dom::parser parser;
	simdjson::dom::element document;
	const std::string text { httpapi::compiledOpenAPIDocument() };
	REQUIRE( parser.parse( text ).get( document ) == simdjson::SUCCESS );
	simdjson::dom::array alternatives;
	REQUIRE(
		document[ "paths" ][ "/relationships/duplicates/add" ][ "post" ][ "requestBody" ][ "content" ]
				[ "application/json" ][ "schema" ][ "anyOf" ]
					.get_array()
					.get( alternatives )
	    == simdjson::SUCCESS );
	REQUIRE( alternatives.size() == 2 );
	for ( const auto alternative : alternatives )
	{
		std::string_view reference;
		const auto error { alternative[ "$ref" ].get_string().get( reference ) };
		if ( error == simdjson::NO_SUCH_FIELD )
		{
			REQUIRE( alternative[ "items" ][ "$ref" ].get_string().get( reference ) == simdjson::SUCCESS );
		}
		else
			REQUIRE( error == simdjson::SUCCESS );

		constexpr std::string_view prefix { "#/components/schemas/" };
		REQUIRE( reference.starts_with( prefix ) );
		simdjson::dom::element schema;
		REQUIRE(
			document[ "components" ][ "schemas" ][ reference.substr( prefix.size() ) ].get( schema )
		    == simdjson::SUCCESS );
		for ( const auto field : { "better_id", "worse_id" } )
		{
			std::string_view type;
			REQUIRE( schema[ "properties" ][ field ][ "type" ].get_string().get( type ) == simdjson::SUCCESS );
			CHECK( type == "integer" );
		}
	}
}

} // namespace idhan::http::test

namespace idhan::http::test
{

TEST_CASE( "Record info structs preserve the metadata JSON contract", "[openapi][response][metadata]" )
{
	httpapi::RecordInfoResponse response {};
	response.record_id = 42;
	response.hashes.sha256 = std::string( 64, 'a' );
	response.tags.push_back( { "default", "test:tag" } );
	response.parsed.emplace( false );
	response.encrypted.emplace( false );

	auto json_e { serializeJson( response ) };
	REQUIRE( json_e.has_value() );
	simdjson::dom::parser parser;
	simdjson::dom::element document;
	REQUIRE( parser.parse( json_e.value() ).get( document ) == simdjson::SUCCESS );
	CHECK( document[ "record_id" ].get_int64().value() == 42 );
	CHECK( document[ "hashes" ][ "sha256" ].get_string().value() == response.hashes.sha256 );
	CHECK( document[ "tags" ].at( 0 )[ "text" ].get_string().value() == "test:tag" );
	CHECK_FALSE( document[ "parsed" ].get_bool().value() );
	CHECK_FALSE( document[ "encrypted" ].get_bool().value() );
	CHECK( document[ "size" ].error() == simdjson::NO_SUCH_FIELD );
	CHECK( document[ "extra" ].error() == simdjson::NO_SUCH_FIELD );
	CHECK( document[ "sha256_hex" ].error() == simdjson::NO_SUCH_FIELD );

	response.parsed.emplace( true );
	response.width.emplace( 1920 );
	response.phash.emplace( "1234" );
	response.extra.emplace( JsonDocument { "{\"parser_field\":123}" } );
	json_e = serializeJson( response );
	REQUIRE( json_e.has_value() );
	REQUIRE( parser.parse( json_e.value() ).get( document ) == simdjson::SUCCESS );
	CHECK( document[ "width" ].get_int64().value() == 1920 );
	CHECK( document[ "phash" ].get_string().value() == "1234" );
	CHECK( document[ "extra" ][ "parser_field" ].get_int64().value() == 123 );
}

TEST_CASE( "Batch metadata structs validate IDs before the handler runs", "[openapi][request][metadata]" )
{
	simdjson::dom::parser parser;
	const std::string valid { "{\"record_ids\":[1,2,1]}" };
	auto body_e { decodeJson< httpapi::FetchRecordMetadataBatchBody >( parser.parse( valid ).value() ) };
	REQUIRE( body_e.has_value() );
	CHECK( body_e.value().record_ids == std::vector< RecordID > { 1, 2, 1 } );

	for ( const std::string invalid :
	      { "{}", "{\"record_ids\":[0]}", "{\"record_ids\":[-1]}", "{\"record_ids\":[\"1\"]}" } )
	{
		auto invalid_e { decodeJson< httpapi::FetchRecordMetadataBatchBody >( parser.parse( invalid ).value() ) };
		CHECK_FALSE( invalid_e.has_value() );
	}
}

TEST_CASE( "Native primary response schemas come from their return types", "[openapi][endpoint]" )
{
	simdjson::dom::parser parser;
	simdjson::dom::element document;
	const std::string text { httpapi::compiledOpenAPIDocument() };
	REQUIRE( parser.parse( text ).get( document ) == simdjson::SUCCESS );
	const auto paths { document[ "paths" ] };
	const auto info { paths[ "/records/{record_id}/info" ][ "get" ] };
	constexpr auto component {
		std::define_static_string( openAPIComponentName< httpapi::RecordInfoResponse, SchemaDirection::output >() )
	};
	CHECK(
		info[ "responses" ][ "200" ][ "content" ][ "application/json" ][ "schema" ][ "$ref" ].get_string().value()
	    == "#/components/schemas/" + std::string { component } );
	CHECK(
		document[ "components" ][ "schemas" ][ component ][ "properties" ][ "hashes" ].error() == simdjson::SUCCESS );
	CHECK( paths[ "/clusters/add" ][ "post" ][ "responses" ][ "201" ].error() == simdjson::SUCCESS );
	CHECK( paths[ "/clusters/add" ][ "post" ][ "responses" ][ "200" ].error() == simdjson::NO_SUCH_FIELD );
}

} // namespace idhan::http::test
