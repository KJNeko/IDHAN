#include <folly/coro/BlockingWait.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <sstream>

#include "httpapi/jsonBody.hpp"
#include "httpapi/parameterDeclarations.hpp"
#include "idhan/http/JsonParameterPath.hpp"
#include "idhan/http/RouteRegistry.hpp"

// Folly includes glog, which also defines CHECK. Use Catch2 assertions below.
#undef CHECK
#include <catch2/catch_test_macros.hpp>

namespace idhan::http::test
{

class ParameterLog
{
	std::shared_ptr< spdlog::logger > previous { spdlog::default_logger() };
	std::ostringstream output {};

  public:

	ParameterLog()
	{
		auto sink { std::make_shared< spdlog::sinks::ostream_sink_mt >( output ) };
		auto logger { std::make_shared< spdlog::logger >( "parameter-tests", sink ) };
		logger->set_pattern( "%v" );
		logger->set_level( spdlog::level::warn );
		spdlog::set_default_logger( std::move( logger ) );
	}

	~ParameterLog() { spdlog::set_default_logger( std::move( previous ) ); }

	[[nodiscard]] std::string text() const { return output.str(); }
};

TEST_CASE( "Query parameter declarations allow repeats and ignore empty separators", "[parameters][query]" )
{
	const std::vector< std::string > allowed { "tag_ids", "idhan_key" };
	for ( const auto query : { "", "&", "&&", "tag_ids=1&tag_ids=2&&idhan_key=secret&" } )
	{
		const auto parameters { RequestParameters::parseQuery( query ) };
		REQUIRE( parameters );
		CHECK( unknownQueryParameters( *parameters, allowed ).empty() );
	}
	const auto parameters { RequestParameters::parseQuery( "tag%5Fids=1&typo=secret&typo=other&second=value" ) };
	REQUIRE( parameters );
	CHECK( unknownQueryParameters( *parameters, allowed ) == std::vector< std::string > { "typo", "second" } );
	const auto empty_name { RequestParameters::parseQuery( "=value" ) };
	REQUIRE( empty_name );
	CHECK( unknownQueryParameters( *empty_name, allowed ) == std::vector< std::string > { "" } );
}

TEST_CASE( "JSON declarations inspect nested fields and leave free-form objects open", "[parameters][json]" )
{
	simdjson::dom::parser parser;
	const simdjson::padded_string input { std::string_view { R"({
        "sort": { "by": "size", "ordr": "asc" },
        "tags": [{ "namespace": "x", "subtag": "y", "typo": 1 }],
        "sets": [
            [{ "namespace": "x", "typo": 1 }],
            [{ "subtag": "y", "extra": 2 }]
        ],
        "extra": { "custom": 1 },
        "bad/name~": 2
    })" } };
	auto document { parser.parse( input ) };
	REQUIRE( !document.error() );
	const std::vector< JsonParameterObject > declarations {
		root().allowFields( { "sort", "tags", "sets", "extra" } ),
		root().field( "sort" ).allowFields( { "by", "order" } ),
		root().field( "tags" ).eachElement().allowFields( { "namespace", "subtag" } ),
		root().field( "sets" ).eachElement().eachElement().allowFields( { "namespace", "subtag" } )
	};
	CHECK(
		unknownJsonParameters( *document, declarations )
	    == std::vector< std::string > {
			"/bad~1name~0", "/sort/ordr", "/tags/0/typo", "/sets/0/0/typo", "/sets/1/0/extra" } );
	CHECK( unknownJsonParameters( *document, {} ).empty() );
}

TEST_CASE( "Warning mode accepts unknown names without logging values or control characters", "[parameters][warnings]" )
{
	ParameterLog log;
	const auto query { RequestParameters::parseQuery( "typo=SUPER_SECRET_VALUE&bad%0Aname=another_secret" ) };
	REQUIRE( query );
	const std::vector< std::string > allowed;
	const auto unknown { unknownQueryParameters( *query, allowed ) };
	CHECK_FALSE( handleUnknownParameters(
		unknown, "query", proxygen::HTTPMethod::GET, "/health", UnknownParameterPolicy::warn ) );
	const auto text { log.text() };
	CHECK( text.find( "GET /health" ) != std::string::npos );
	CHECK( text.find( "typo" ) != std::string::npos );
	CHECK( text.find( "bad\\nname" ) != std::string::npos );
	CHECK( text.find( "bad\nname" ) == std::string::npos );
	CHECK( text.find( "SUPER_SECRET_VALUE" ) == std::string::npos );
	CHECK( text.find( "another_secret" ) == std::string::npos );
}

static HTTPTask< Response > parameterTestHandler( RequestContext )
{
	co_return Response::bytes( 200, folly::IOBuf::copyBuffer( "ok" ), "text/plain" );
}

TEST_CASE(
	"Route query checks support future 400 responses and recognize path captures",
	"[parameters][query][reject]" )
{
	RouteRegistry routes;
	ParameterRules rules { { "idhan_key" }, {}, UnknownParameterPolicy::reject };
	routes.add(
		RouteDefinition {
			RouteMetadata {
				proxygen::HTTPMethod::GET,
				"/records/{record_id}",
				{ { "record_id", []( const std::string_view ) -> std::expected< void, ParseError > { return {}; } } },
				{},
				rules },
			parameterTestHandler } );
	routes.seal();
	const auto match { routes.match( proxygen::HTTPMethod::GET, "/records/1" ) };
	REQUIRE( match.status == RouteMatch::Status::found );
	proxygen::HTTPMessage message;
	message.setMethod( proxygen::HTTPMethod::GET );
	message.setURL( "/records/1" );
	auto parameters { match.path_parameters };
	parameters.append( "idhan_key", "secret" );
	CHECK_FALSE( folly::coro::blockingWait( routes.validateRequest( match, message, parameters ) ) );
	parameters.append( "typo", "secret_value" );
	const auto rejected { folly::coro::blockingWait( routes.validateRequest( match, message, parameters ) ) };
	REQUIRE( rejected );
	CHECK( rejected->message.getStatusCode() == 400 );
}

TEST_CASE( "The JSON body reader warns today and can reject by policy", "[parameters][json][reject]" )
{
	ParameterLog log;
	runtime::DrainRegistry work;
	for ( const auto policy : { UnknownParameterPolicy::warn, UnknownParameterPolicy::reject } )
	{
		ParameterRules rules { {}, { root().allowFields( { "known" } ) }, policy };
		proxygen::HTTPMessage message;
		message.setMethod( proxygen::HTTPMethod::POST );
		message.setURL( "/example" );
		RequestContext context {
			1,
			std::move( message ),
			RequestParameters {},
			folly::IOBuf::copyBuffer( R"({"known":1,"typo":"SECRET_JSON_VALUE"})" ),
			std::move( *work.admit() )
		};
		context.setParameterRules( &rules );
		const auto body { httpapi::readValidatedJsonBody( context ) };
		if ( policy == UnknownParameterPolicy::warn )
		{
			REQUIRE( body );
			CHECK( body->find( "SECRET_JSON_VALUE" ) != std::string::npos );
		}
		else
		{
			REQUIRE_FALSE( body );
			CHECK( body.error().message.getStatusCode() == 400 );
		}
	}
	CHECK( log.text().find( "/typo" ) != std::string::npos );
	CHECK( log.text().find( "SECRET_JSON_VALUE" ) == std::string::npos );
}

TEST_CASE( "Native routes require explicit warn-mode declarations", "[parameters][declarations]" )
{
	auto route { httpapi::declareEndpointParameters(
		RouteDefinition { RouteMetadata { proxygen::HTTPMethod::POST, "/search", {}, {} }, parameterTestHandler } ) };
	REQUIRE( route.metadata.parameters );
	CHECK( route.metadata.parameters->unknown == UnknownParameterPolicy::warn );
	CHECK( route.metadata.parameters->query == std::vector< std::string > { "idhan_key" } );
	CHECK( route.metadata.parameters->json.size() == 2 );
	CHECK_THROWS_AS(
		httpapi::declareEndpointParameters(
			RouteDefinition { RouteMetadata { proxygen::HTTPMethod::GET, "/undeclared-native-route", {}, {} },
			                  parameterTestHandler } ),
		std::invalid_argument );
}

} // namespace idhan::http::test
