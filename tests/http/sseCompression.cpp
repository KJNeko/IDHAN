#include "idhan/http/HTTPServer.hpp"
#include "idhan/http/SSEBodySource.hpp"

#include "../api/harness/JobEventStream.hpp"

// Folly includes glog, which also defines CHECK.
#undef CHECK
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace idhan::http::test
{

static HTTPTask< Response > subscribeToEvents( SSEEventHub& events, RequestContext )
{
	auto initial_e { ValidatedJson::parse( R"({"value":1})" ) };
	if ( !initial_e.has_value() ) throw std::logic_error( "Invalid test snapshot" );

	Response response;
	response.body = proxygen::coro::HTTPSourceHolder { new SSEBodySource {
		std::move( response.message ), events.subscribe( {}, { std::move( initial_e.value() ) }, false ) } };
	co_return response;
}

TEST_CASE( "SSE compression delivers snapshots and updates before stream end", "[http][sse][compression]" )
{
	const auto encoding { GENERATE( "identity", "gzip", "zstd" ) };
	CAPTURE( encoding );
	SSEEventHub events;
	RouteRegistry routes;
	routes.add( RouteDefinition {
		RouteMetadata { proxygen::HTTPMethod::GET, "/events", {}, {} },
		[ &events ]( RequestContext context ) { return subscribeToEvents( events, std::move( context ) ); } } );
	routes.seal();
	HTTPConfig config;
	config.tls = false;
	config.port = 0;
	config.io_threads = 1;
	config.application_threads = 1;
	config.heavy_threads = 1;
	HTTPServer server { std::move( config ), std::move( routes ) };
	server.start();

	idhan::test::JobEventStream stream { server.port(), "", "/events", encoding };
	const auto snapshot { stream.waitFor( "snapshot" ) };
	REQUIRE( snapshot[ "value" ].asInt() == 1 );
	CHECK( stream.contentEncoding() == ( std::string_view { encoding } == "identity" ? "" : encoding ) );

	auto update_e { ValidatedJson::parse( R"({"value":2})" ) };
	REQUIRE( update_e.has_value() );
	events.publish( "entity", std::move( update_e.value() ) );
	const auto update { stream.waitFor( "update" ) };
	CHECK( update[ "value" ].asInt() == 2 );
	events.shutdown();
}

} // namespace idhan::http::test
