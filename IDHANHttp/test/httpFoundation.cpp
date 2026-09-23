#include <catch2/catch_test_macros.hpp>
#include <folly/coro/BlockingWait.h>
#include <sys/stat.h>

#include <array>
#include <cerrno>
#include <fcntl.h>
#include <stdexcept>

#include "CurlHelper.hpp"
#include "EchoServer.hpp"
#include "idhan/SHA256.hpp"
#include "idhan/http/HTTPConfig.hpp"
#include "idhan/http/RequestParameters.hpp"
#include "idhan/http/RouteRegistry.hpp"
#include "idhan/http/SSEEventHub.hpp"
#include "idhan/http/TemporaryUpload.hpp"
#include "idhan/http/UploadPipeline.hpp"
#include "idhan/http/ValidatedJson.hpp"

using namespace idhan::http;

TEST_CASE( "Spilled bodies own an unlinked close-on-exec file", "[http][upload]" )
{
	int descriptor { -1 };
	{
		UploadPipeline pipeline { std::filesystem::temp_directory_path() };
		const std::string prefix( request_memory_limit, 'x' );
		folly::coro::blockingWait( pipeline.append( folly::IOBuf::copyBuffer( prefix ) ) );
		folly::coro::blockingWait( pipeline.append( folly::IOBuf::copyBuffer( "y" ) ) );
		auto body { folly::coro::blockingWait( pipeline.finish() ) };
		auto& upload { std::get< TemporaryUpload >( body ) };
		REQUIRE( upload.size() == request_memory_limit + 1 );
		REQUIRE( upload.filename().empty() );
		auto file { std::move( upload ).releaseFile() };
		descriptor = file.fd();
		struct stat metadata {};
		REQUIRE( ::fstat( descriptor, &metadata ) == 0 );
		REQUIRE( metadata.st_nlink == 0 );
		REQUIRE( ( ::fcntl( descriptor, F_GETFD ) & FD_CLOEXEC ) != 0 );
	}
	REQUIRE( ::fcntl( descriptor, F_GETFD ) == -1 );
	REQUIRE( errno == EBADF );
}

TEST_CASE( "Temporary storage is needed only after the body crosses the threshold", "[http][upload]" )
{
	UploadPipeline pipeline { "/dev/null/idhan-upload" };
	const std::string prefix( request_memory_limit, 'x' );
	REQUIRE_NOTHROW( folly::coro::blockingWait( pipeline.append( folly::IOBuf::copyBuffer( prefix ) ) ) );
	REQUIRE_THROWS( folly::coro::blockingWait( pipeline.append( folly::IOBuf::copyBuffer( "y" ) ) ) );
}

TEST_CASE( "Request bodies spill to disk only above eight MiB", "[http][upload][integration]" )
{
	std::size_t size {};
	bool chunked {};
	SECTION( "Empty body" )
	{}
	SECTION( "Below threshold" )
	{
		size = request_memory_limit - 1;
	}
	SECTION( "Exactly at threshold" )
	{
		size = request_memory_limit;
	}
	SECTION( "One byte above threshold" )
	{
		size = request_memory_limit + 1;
	}
	SECTION( "Multiple file chunks" )
	{
		size = request_memory_limit + 3 * file_chunk_bytes + 17;
	}
	SECTION( "Chunked request without Content-Length" )
	{
		size = request_memory_limit + 1;
		chunked = true;
	}
	test::EchoServer server;
	server.start();
	idhan::test::CurlRequest request;
	request.url = idhan::test::localUrl( server.port(), "/echo" );
	request.method = "POST";
	request.body.resize( size );
	for ( std::size_t index {}; index < size; ++index ) request.body[ index ] = static_cast< char >( index % 251 );
	request.headers.emplace_back( "Content-Type: application/octet-stream" );
	if ( chunked ) request.headers.emplace_back( "Transfer-Encoding: chunked" );

	const auto response { idhan::test::send( request ) };
	REQUIRE( response.status == 200 );
	REQUIRE( response.body == request.body );
	REQUIRE( response.header( "X-Echo-Body-Storage" ) == ( size > request_memory_limit ? "file" : "memory" ) );
}

TEST_CASE( "Temporary uploads own their metadata and descriptor before MIME detection", "[http][upload]" )
{
	std::array< std::byte, idhan::SHA256::size() > bytes {};
	bytes[ 0 ] = std::byte { 0x2a };
	const auto hash { idhan::SHA256::fromBuffer( bytes ) };
	TemporaryUpload upload { folly::File::temporary(), 42, hash, "example.bin" };

	REQUIRE( upload.size() == 42 );
	REQUIRE( upload.hash() == hash );
	REQUIRE( upload.filename() == "example.bin" );

	auto duplicate { upload.duplicateFile() };
	REQUIRE( duplicate );

	auto released { std::move( upload ).releaseFile() };
	REQUIRE( released );
}

TEST_CASE( "Query parameters retain ordering and reject scalar duplicates", "[http][parameters]" )
{
	auto parameters { RequestParameters::parseQuery( "record_id=3&tag=blue+sky&record_id=5&tag=red%2Bgreen" ) };
	REQUIRE( parameters );
	REQUIRE( parameters->vector< std::int32_t >( "record_id" ) == std::vector< std::int32_t > { 3, 5 } );
	REQUIRE_FALSE( parameters->scalar< std::int32_t >( "record_id" ) );
	REQUIRE( parameters->vector< std::string >( "tag" ) == std::vector< std::string > { "blue sky", "red+green" } );
}

TEST_CASE( "Query parameter decoding rejects malformed escapes and NUL", "[http][parameters]" )
{
	REQUIRE_FALSE( RequestParameters::parseQuery( "name=%" ) );
	REQUIRE_FALSE( RequestParameters::parseQuery( "name=%0" ) );
	REQUIRE_FALSE( RequestParameters::parseQuery( "name=%QQ" ) );
	REQUIRE_FALSE( RequestParameters::parseQuery( "name=%00" ) );
}

TEST_CASE( "Boolean query parameters accept true false and numeric spellings", "[http][parameters]" )
{
	auto parameters { RequestParameters::parseQuery( "enabled=true&disabled=0&numeric=1&invalid=yes" ) };
	REQUIRE( parameters );
	REQUIRE( parameters->scalar< bool >( "enabled" ) );
	REQUIRE_FALSE( *parameters->scalar< bool >( "disabled" ) );
	REQUIRE( parameters->scalar< bool >( "numeric" ) );
	REQUIRE_FALSE( parameters->optional< bool >( "invalid" ) );
	REQUIRE_FALSE( parameters->optional< bool >( "absent" )->has_value() );
}

TEST_CASE( "ValidatedJson retains only syntactically valid JSON", "[http][json]" )
{
	const auto valid { ValidatedJson::parse( R"({"record_id":3,"tags":["blue"]})" ) };
	REQUIRE( valid );
	REQUIRE( valid->text() == R"({"record_id":3,"tags":["blue"]})" );
	REQUIRE_FALSE( ValidatedJson::parse( R"({"record_id":)" ) );
}

TEST_CASE( "SSE subscriptions receive a baseline, coalesced updates, heartbeats, and shutdown", "[http][sse]" )
{
	SSEEventHub hub;
	const auto initial { ValidatedJson::parse( R"({"id":3,"state":"queued"})" ) };
	REQUIRE( initial );
	hub.publish( "session:3", *initial );

	auto subscription { hub.subscribe( { "session:3" } ) };
	auto snapshot { folly::coro::blockingWait( subscription.next() ) };
	REQUIRE( snapshot );
	REQUIRE( snapshot->kind == SSEEvent::Kind::snapshot );
	REQUIRE( snapshot->entity == "session:3" );
	REQUIRE( snapshot->data->text() == initial->text() );

	const auto first_update { ValidatedJson::parse( R"({"id":3,"state":"running"})" ) };
	const auto second_update { ValidatedJson::parse( R"({"id":3,"state":"done"})" ) };
	REQUIRE( first_update );
	REQUIRE( second_update );
	hub.publish( "session:3", *first_update );
	hub.publish( "session:3", *second_update );

	auto update { folly::coro::blockingWait( subscription.next() ) };
	REQUIRE( update );
	REQUIRE( update->kind == SSEEvent::Kind::update );
	REQUIRE( update->data->text() == second_update->text() );

	hub.heartbeat();
	auto heartbeat { folly::coro::blockingWait( subscription.next() ) };
	REQUIRE( heartbeat );
	REQUIRE( heartbeat->kind == SSEEvent::Kind::keepalive );
	REQUIRE_FALSE( heartbeat->data );

	hub.shutdown();
	auto shutdown { folly::coro::blockingWait( subscription.next() ) };
	REQUIRE( shutdown );
	REQUIRE( shutdown->kind == SSEEvent::Kind::shutdown );
	REQUIRE_FALSE( folly::coro::blockingWait( subscription.next() ) );
}

TEST_CASE( "HTTP configuration rejects invalid listener and flow-control settings", "[http][configuration]" )
{
	const auto defaults { HTTPConfig::defaults() };
	REQUIRE_NOTHROW( defaults.validate() );

	auto no_listener { defaults };
	no_listener.ipv4_listen.clear();
	no_listener.ipv6_listen.clear();
	REQUIRE_THROWS_AS( no_listener.validate(), std::invalid_argument );

	auto invalid_window { defaults };
	invalid_window.stream_receive_window = 0;
	REQUIRE_THROWS_AS( invalid_window.validate(), std::invalid_argument );
}

TEST_CASE( "Routing prioritizes literal segments and explicit HEAD routes", "[http][routing]" )
{
	auto route = []( const proxygen::HTTPMethod method, std::string path )
	{ return RouteDefinition { RouteMetadata { method, std::move( path ), {}, {} }, {} }; };
	RouteRegistry registry;
	auto literal { route( proxygen::HTTPMethod::GET, "/files/static" ) };
	registry.add( std::move( literal ) );

	auto typed { route( proxygen::HTTPMethod::POST, "/files/{record_id}" ) };
	typed.metadata.path_parameters.push_back(
		PathParameter {
			"record_id",
			[]( const std::string_view value ) -> std::expected< void, ParseError >
			{
				if ( value.empty() ) return std::unexpected( ParseError { "record_id", "is required" } );

				return {};
			} } );
	registry.add( std::move( typed ) );

	auto get_health { route( proxygen::HTTPMethod::GET, "/health" ) };
	registry.add( std::move( get_health ) );

	auto head_health { route( proxygen::HTTPMethod::HEAD, "/health" ) };
	registry.add( std::move( head_health ) );
	registry.seal();

	const auto literal_match { registry.match( proxygen::HTTPMethod::POST, "/files/static" ) };
	REQUIRE( literal_match.status == RouteMatch::Status::method_not_allowed );
	REQUIRE( literal_match.allowed_methods == std::vector { proxygen::HTTPMethod::GET } );

	const auto head_match { registry.match( proxygen::HTTPMethod::HEAD, "/health" ) };
	REQUIRE( head_match.status == RouteMatch::Status::found );
	REQUIRE( head_match.route_id == std::optional< std::size_t > { 3 } );
}

TEST_CASE( "Registration rejects paths whose parameters do not line up", "[http][routing]" )
{
	auto parameter = []( std::string name )
	{
		return PathParameter {
			std::move( name ), []( std::string_view ) -> std::expected< void, ParseError > { return {}; }
		};
	};

	auto route = []( std::string path, std::vector< PathParameter > parameters )
	{
		return RouteDefinition {
			RouteMetadata { proxygen::HTTPMethod::DELETE, std::move( path ), std::move( parameters ), {} }, {}
		};
	};

	RouteRegistry registry;

	REQUIRE_THROWS_AS(
		registry.add( route( "/records/{record_id}/remove_note/{note_id}", { parameter( "record_id" ) } ) ),
		std::invalid_argument );

	REQUIRE_THROWS_AS(
		registry.add( route( "/records/{record_id}", { parameter( "record_id" ), parameter( "note_id" ) } ) ),
		std::invalid_argument );

	REQUIRE_THROWS_AS(
		registry.add( route( "/records/{record_id}", { parameter( "wrong_name" ) } ) ), std::invalid_argument );

	REQUIRE_NOTHROW( registry.add(
		route( "/records/{record_id}/remove_note/{note_id}", { parameter( "record_id" ), parameter( "note_id" ) } ) ) );
}

TEST_CASE( "A catch-all segment captures the rest of the path", "[http][routing]" )
{
	RouteRegistry registry;

	auto rejects_dots = []( const std::string_view value ) -> std::expected< void, ParseError >
	{
		if ( value.find( ".." ) != std::string_view::npos )
			return std::unexpected( ParseError { "path", "must not traverse" } );
		return {};
	};

	registry.add(
		RouteDefinition {
			RouteMetadata { proxygen::HTTPMethod::GET, "/api/{*path}", { PathParameter { "path", rejects_dots } }, {} },
			{} } );
	registry.add( RouteDefinition { RouteMetadata { proxygen::HTTPMethod::GET, "/api", {}, {} }, {} } );
	registry.seal();

	const auto nested { registry.match( proxygen::HTTPMethod::GET, "/api/paths/auth/generate_key.yaml" ) };
	REQUIRE( nested.status == RouteMatch::Status::found );
	REQUIRE( nested.path_parameters.scalar< std::string >( "path" ) == "paths/auth/generate_key.yaml" );

	const auto single { registry.match( proxygen::HTTPMethod::GET, "/api/index.yaml" ) };
	REQUIRE( single.status == RouteMatch::Status::found );
	REQUIRE( single.path_parameters.scalar< std::string >( "path" ) == "index.yaml" );

	const auto bare { registry.match( proxygen::HTTPMethod::GET, "/api" ) };
	REQUIRE( bare.status == RouteMatch::Status::found );
	REQUIRE( bare.route_id == std::optional< std::size_t > { 1 } );

	REQUIRE(
		registry.match( proxygen::HTTPMethod::GET, "/api/../secrets" ).status
		== RouteMatch::Status::invalid_parameter );
}

TEST_CASE( "A catch-all must be the last segment", "[http][routing]" )
{
	RouteRegistry registry;
	auto accepts = []( std::string_view ) -> std::expected< void, ParseError > { return {}; };

	REQUIRE_THROWS_AS(
		registry.add(
			RouteDefinition {
				RouteMetadata {
					proxygen::HTTPMethod::GET, "/api/{*path}/tail", { PathParameter { "path", accepts } }, {} },
				{} } ),
		std::invalid_argument );
}

TEST_CASE( "Cross-origin requests preflight and carry their headers", "[http][integration][cors]" )
{
	test::EchoServer server;
	server.start();

	idhan::test::CurlRequest preflight {};
	preflight.url = idhan::test::localUrl( server.port(), "/echo" );
	preflight.method = "OPTIONS";
	preflight.headers = { "Origin: https://elsewhere.example", "Access-Control-Request-Method: POST" };

	const auto answered { idhan::test::send( preflight ) };
	REQUIRE( answered.status == 204 );
	REQUIRE( answered.header( "Access-Control-Allow-Origin" ) == "*" );
	REQUIRE( answered.header( "Access-Control-Allow-Methods" ) == "POST, OPTIONS" );

	auto unknown { preflight };
	unknown.url = idhan::test::localUrl( server.port(), "/no-such-route" );
	REQUIRE( idhan::test::send( unknown ).status == 404 );

	const auto allowed {
		idhan::test::post( idhan::test::localUrl( server.port(), "/echo" ), "body", "application/x-idhan-test" )
	};
	REQUIRE( allowed.header( "Access-Control-Allow-Origin" ) == "*" );

	const auto rejected { idhan::test::get( idhan::test::localUrl( server.port(), "/echo" ) ) };
	REQUIRE( rejected.status == 405 );
	REQUIRE( rejected.header( "Access-Control-Allow-Origin" ) == "*" );
}

TEST_CASE( "The echo server exposes its registered endpoints", "[http][integration]" )
{
	test::EchoServer server;
	server.start();
	const auto echo { idhan::test::post(
		idhan::test::localUrl( server.port(), "/echo" ),
		std::string { "echo\0bytes", 10 },
		"application/x-idhan-test" ) };
	REQUIRE( echo.status == 200 );
	REQUIRE( echo.body == std::string { "echo\0bytes", 10 } );
	REQUIRE( echo.header( "Content-Type" ) == "application/x-idhan-test" );

	const auto echo_with_get { idhan::test::get( idhan::test::localUrl( server.port(), "/echo" ) ) };
	REQUIRE( echo_with_get.status == 405 );
	REQUIRE( echo_with_get.header( "Allow" ) == "POST" );

	const auto parameter { idhan::test::get( idhan::test::localUrl( server.port(), "/42/echo" ) ) };
	REQUIRE( parameter.status == 200 );
	REQUIRE( parameter.header( "Content-Type" ) == "text/plain; charset=utf-8" );
	REQUIRE( parameter.body == "42" );

	const auto invalid_parameter { idhan::test::get( idhan::test::localUrl( server.port(), "/not-an-id/echo" ) ) };
	REQUIRE( invalid_parameter.status == 400 );

	const auto parameters { idhan::test::get(
		idhan::test::localUrl( server.port(), "/echo/parameters?required_parameter=blue&tag=green&tag=red" ) ) };
	REQUIRE( parameters.status == 200 );
	REQUIRE( parameters.header( "Content-Type" ) == "application/json" );
	REQUIRE(
		parameters.body
		== R"([{"name":"required_parameter","value":"blue"},{"name":"tag","value":"green"},{"name":"tag","value":"red"}])" );

	const auto missing_parameter { idhan::test::get( idhan::test::localUrl( server.port(), "/echo/parameters" ) ) };
	REQUIRE( missing_parameter.status == 400 );

	const auto unknown { idhan::test::get( idhan::test::localUrl( server.port(), "/unknown/path" ) ) };
	REQUIRE( unknown.status == 404 );

	const auto version { idhan::test::get( idhan::test::localUrl( server.port(), "/version" ) ) };
	REQUIRE( version.status == 200 );
	REQUIRE( version.header( "Content-Type" ) == "application/json" );
	REQUIRE( version.body == R"({"name":"IDHANHttp","version":"0.2.0"})" );
}
