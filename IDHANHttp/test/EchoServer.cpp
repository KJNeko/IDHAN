#include "EchoServer.hpp"

#include <folly/FileUtil.h>
#include <folly/io/IOBuf.h>

#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <memory>
#include <simdjson.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace idhan::http::test
{

namespace echo_routes
{

// The echo example chooses to load file-backed bodies for its buffered response.
static HTTPTask< std::unique_ptr< folly::IOBuf > > readEchoFile( TemporaryUpload upload )
{
	auto file { std::move( upload ).releaseFile() };
	auto bytes { folly::IOBuf::create( upload.size() ) };
	const auto read { folly::preadFull( file.fd(), bytes->writableData(), upload.size(), 0 ) };
	if ( read < 0 || static_cast< std::uint64_t >( read ) != upload.size() )
		throw std::runtime_error( "Cannot read echo upload" );

	bytes->append( upload.size() );
	co_return bytes;
}

[[nodiscard]] HTTPTask< Response > echo( folly::CPUThreadPoolExecutor& executor, RequestContext context )
{
	auto content_type { context.message().getHeaders().getSingleOrEmpty( "Content-Type" ) };
	if ( content_type.empty() ) content_type = "application/octet-stream";

	auto body { std::move( context ).takeBody() };
	const bool on_disk { std::holds_alternative< TemporaryUpload >( body ) };
	std::unique_ptr< folly::IOBuf > bytes;
	if ( on_disk )
	{
		bytes = co_await readEchoFile( std::move( std::get< TemporaryUpload >( body ) ) ).scheduleOn( &executor );
	}
	else
		bytes = std::move( std::get< std::unique_ptr< folly::IOBuf > >( body ) );

	auto response { Response::bytes( 200, std::move( bytes ), std::string { content_type } ) };
	response.message.getHeaders().set( "X-Echo-Body-Storage", on_disk ? "file" : "memory" );
	co_return response;
}

[[nodiscard]] HTTPTask< Response > version( [[maybe_unused]] RequestContext context )
{
	const auto body { std::format(
		R"({{"name":"IDHANHttp","version":"{}.{}.{}"}})",
		IDHANHttp_MAJOR_VERSION,
		IDHANHttp_MINOR_VERSION,
		IDHANHttp_PATCH_VERSION ) };
	co_return Response::json( 200, folly::IOBuf::copyBuffer( body ) );
}

struct ParameterEchoRequest
{
	std::uint64_t id;
};

struct EchoParametersRequest
{
	std::string required_parameter;
};

[[nodiscard]] std::expected< void, ParseError > validateIdentifier( const std::string_view value )
{
	auto identifier { parseParameter< std::uint64_t >( "id", value ) };
	if ( !identifier ) return std::unexpected( std::move( identifier.error() ) );

	return {};
}

[[nodiscard]] std::expected< ParameterEchoRequest, ParseError > parseParameterEchoRequest(
	const RequestContext& context )
{
	auto identifier { context.parameters().scalar< std::uint64_t >( "id" ) };
	if ( !identifier ) return std::unexpected( std::move( identifier.error() ) );

	return ParameterEchoRequest { *identifier };
}

[[nodiscard]] HTTPTask< Response > parameterEcho(
	[[maybe_unused]] RequestContext context,
	ParameterEchoRequest request )
{
	const auto body { std::format( "{}", request.id ) };
	co_return Response::bytes( 200, folly::IOBuf::copyBuffer( body ), "text/plain; charset=utf-8" );
}

[[nodiscard]] std::expected< EchoParametersRequest, ParseError > parseEchoParametersRequest(
	const RequestContext& context )
{
	auto required_parameter { context.parameters().scalar< std::string >( "required_parameter" ) };
	if ( !required_parameter ) return std::unexpected( std::move( required_parameter.error() ) );

	return EchoParametersRequest { std::move( *required_parameter ) };
}

[[nodiscard]] std::unique_ptr< folly::IOBuf > serializeParameters( const RequestParameters& parameters )
{
	simdjson::builder::string_builder builder;
	builder.start_array();
	bool first { true };
	for ( const auto& [ name, value ] : parameters.values() )
	{
		if ( !first ) builder.append_comma();

		builder.start_object();
		builder.append_key_value( "name", name );
		builder.append_comma();
		builder.append_key_value( "value", value );
		builder.end_object();
		first = false;
	}
	builder.end_array();

	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	return folly::IOBuf::copyBuffer( *json );
}

[[nodiscard]] HTTPTask< Response > echoParameters( RequestContext context, EchoParametersRequest request )
{
	if ( request.required_parameter.empty() ) co_return Response::error( 400, "required_parameter is required" );

	co_return Response::json( 200, serializeParameters( context.parameters() ) );
}

[[nodiscard]] RouteDefinition route(
	const proxygen::HTTPMethod method,
	std::string path,
	std::function< HTTPTask< Response >( RequestContext ) > handler )
{
	return RouteDefinition { RouteMetadata { method, std::move( path ), {}, {} }, std::move( handler ) };
}

[[nodiscard]] RouteRegistry echoRoutes( folly::CPUThreadPoolExecutor& executor )
{
	RouteRegistry routes;
	routes.add( route( proxygen::HTTPMethod::POST, "/echo", std::bind_front( echo, std::ref( executor ) ) ) );
	routes.add( route( proxygen::HTTPMethod::GET, "/version", version ) );

	auto parameter_echo { makeRoute(
		RouteSpec< ParameterEchoRequest > {
			RouteMetadata {
				proxygen::HTTPMethod::GET, "/{id}/echo", { PathParameter { "id", validateIdentifier } }, {} },
			parseParameterEchoRequest,
			parameterEcho } ) };
	routes.add( std::move( parameter_echo ) );

	auto echo_parameters { makeRoute(
		RouteSpec< EchoParametersRequest > {
			RouteMetadata { proxygen::HTTPMethod::GET, "/echo/parameters", {}, {} },
			parseEchoParametersRequest,
			echoParameters } ) };
	routes.add( std::move( echo_parameters ) );
	routes.seal();
	return routes;
}

[[nodiscard]] HTTPConfig echoConfig()
{
	auto config { HTTPConfig::defaults() };
	config.port = 0;
	config.tls = false;
	config.ipv6_listen.clear();
	config.io_threads = 1;
	return config;
}
} // namespace echo_routes

using namespace echo_routes;

EchoServer::EchoServer() : m_server( echoConfig(), echoRoutes( m_file_executor ) )
{}

void EchoServer::start()
{
	m_server.start();
}

void EchoServer::stop()
{
	m_server.stop();
}

std::uint16_t EchoServer::port() const noexcept
{
	return m_server.port();
}
} // namespace idhan::http::test
