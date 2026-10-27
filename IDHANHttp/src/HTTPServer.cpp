#include "idhan/http/HTTPServer.hpp"

#include <folly/synchronization/Baton.h>
#include <proxygen/lib/http/coro/HTTPFixedSource.h>
#include <proxygen/lib/http/coro/filters/CompressionFilterFactory.h>
#include <proxygen/lib/http/coro/server/handlers/ExpectContinueWrapperHandler.h>
#include <spdlog/spdlog.h>

#include <exception>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "idhan/http/UploadPipeline.hpp"

namespace idhan::http
{
[[nodiscard]] static std::shared_ptr< const std::set< std::string > > compressibleContentTypes()
{
	static const std::shared_ptr< const std::set< std::string > > types {
		std::make_shared< const std::set< std::string > >( std::set< std::string > {
			"application/json",
			"text/plain",
			"text/event-stream",
			"text/html",
			"text/css",
			"text/javascript",
			"application/javascript",
			"application/xml",
			"text/xml",
			"image/svg+xml" } )
	};
	return types;
}

[[nodiscard]] static proxygen::coro::HTTPServer::Config makeProxygenConfig( const HTTPConfig& config )
{
	proxygen::coro::HTTPServer::Config proxygen_config;
	const auto& listen_address { config.ipv4_listen.empty() ? config.ipv6_listen : config.ipv4_listen };
	proxygen_config.socketConfig.bindAddress.setFromIpPort( listen_address, config.port );
	proxygen_config.socketConfig.enableReuseAddr = true;
	proxygen_config.numIOThreads = config.io_threads;
	proxygen_config.shutdownOnSignals.clear();
	proxygen_config.sessionConfig.streamReadTimeout = config.upload_idle_timeout;
	proxygen_config.sessionConfig.connIdleTimeout = config.keepalive_timeout;
	proxygen_config.sessionConfig.writeTimeout = config.response_idle_timeout;
	proxygen_config.sessionConfig.streamFlowControl = config.stream_receive_window;
	proxygen_config.sessionConfig.connFlowControl = config.connection_receive_window;
	proxygen_config.sessionConfig.settings.emplace_back(
		proxygen::SettingsId::MAX_CONCURRENT_STREAMS, config.max_concurrent_streams );

	proxygen::CompressionFilterUtils::FactoryOptions compression_options {};
	compression_options.minimumCompressionSize = compression_threshold;
	compression_options.compressibleContentTypes = compressibleContentTypes();
	compression_options.zlibCompressionLevel = config.gzip_level;
	compression_options.zstdCompressionLevel = config.zstd_level;
	compression_options.enableGzip = true;
	compression_options.enableZstd = true;
	proxygen_config.filterFactories.push_back(
		std::make_shared< proxygen::coro::ServerCompressionFilterFactory >( compression_options ) );

	if ( !config.tls )
	{
		proxygen_config.plaintextProtocol = "http/1.1";
		return proxygen_config;
	}

	ensureSelfSignedCertificate( config );

	auto tls_config { proxygen::coro::HTTPServer::getDefaultTLSConfig() };
	tls_config.setCertificate( config.certificate.string(), config.private_key.string(), "" );
	proxygen_config.socketConfig.sslContextConfigs.emplace_back( std::move( tls_config ) );
	return proxygen_config;
}

[[nodiscard]] static std::string allowHeader( const std::vector< proxygen::HTTPMethod >& methods )
{
	std::string allow;
	for ( const auto method : methods )
	{
		if ( !allow.empty() ) allow += ", ";

		allow += proxygen::methodToString( method );
	}
	return allow;
}

[[nodiscard]] static RequestParameters combineParameters(
	RequestParameters query_parameters,
	const RequestParameters& path_parameters )
{
	for ( const auto& [ name, value ] : path_parameters.values() ) query_parameters.append( name, value );

	return query_parameters;
}

static void addCorsHeaders( proxygen::HTTPMessage& message )
{
	auto& headers { message.getHeaders() };
	headers.set( "Access-Control-Allow-Origin", "*" );
	headers.set( "Access-Control-Allow-Headers", "*" );
	headers.set( "Access-Control-Max-Age", "86400" );
}

[[nodiscard]] static proxygen::coro::HTTPSourceHolder makeResponseSource( Response response, const bool suppress_body )
{
	if ( auto source { std::get_if< proxygen::coro::HTTPSourceHolder >( &response.body ) } )
		return std::move( *source );

	addCorsHeaders( response.message );

	auto message { std::make_unique< proxygen::HTTPMessage >( std::move( response.message ) ) };
	if ( suppress_body ) return proxygen::coro::HTTPFixedSource::makeFixedSource( std::move( message ) );

	if ( auto body { std::get_if< std::unique_ptr< folly::IOBuf > >( &response.body ) } )
		return proxygen::coro::HTTPFixedSource::makeFixedSource( std::move( message ), std::move( *body ) );

	return proxygen::coro::HTTPFixedSource::makeFixedSource( std::move( message ) );
}

[[nodiscard]] static Response routeError( const RouteMatch& match )
{
	std::uint16_t status { 405 };
	std::string message { "Method not allowed" };
	if ( match.status == RouteMatch::Status::not_found )
	{
		status = 404;
		message = "Not found";
	}
	else if ( match.status == RouteMatch::Status::invalid_parameter )
	{
		status = 400;
		message = "Invalid route parameter";
	}

	auto response { Response::error( status, std::move( message ) ) };
	if ( status == 405 ) response.message.getHeaders().set( "Allow", allowHeader( match.allowed_methods ) );

	return response;
}

[[nodiscard]] static Response preflightResponse( const RouteMatch& match )
{
	const bool not_found { match.status == RouteMatch::Status::not_found };
	Response response { not_found ? Response::error( 404, "Not found" ) : Response {} };
	if ( not_found ) return response;

	auto methods { match.allowed_methods };
	methods.push_back( proxygen::HTTPMethod::OPTIONS );

	response.message.setStatusCode( 204 );
	response.message.setStatusMessage( proxygen::HTTPMessage::getDefaultReason( 204 ) );
	response.message.getHeaders().set( "Access-Control-Allow-Methods", allowHeader( methods ) );
	response.body = std::monostate {};
	return response;
}

[[nodiscard]] static HTTPTask< proxygen::coro::HTTPSourceHolder > readBodyAndDispatch(
	const RouteRegistry& routes,
	RouteMatch match,
	RequestContext context,
	proxygen::coro::HTTPSourceHolder source,
	bool end_of_message,
	const HTTPConfig& config,
	folly::CPUThreadPoolExecutor& executor )
{
	const bool head { context.message().getMethod() == proxygen::HTTPMethod::HEAD };
	const auto method { context.message().getMethod() };
	const auto path_piece { context.message().getPathAsStringPiece() };
	const std::string path { path_piece.data(), path_piece.size() };
	const auto method_name { method ? proxygen::methodToString( *method ) : "?" };
	Response response;
	try
	{
		UploadPipeline body { config.temp_path };
		while ( !end_of_message )
		{
			auto event { co_await source.readBodyEvent( 256 * 1024 ) };
			end_of_message = event.eom;
			if ( event.eventType == proxygen::coro::HTTPBodyEvent::BODY )
			{
				if ( !end_of_message ) source.setReadTimeout( std::chrono::milliseconds { 0 } );

				// co_await body.append( event.event.body.move() ).scheduleOn( &executor );
				co_await folly::coro::co_withExecutor( &executor, body.append( event.event.body.move() ) );
				if ( !end_of_message ) source.setReadTimeout( config.upload_idle_timeout );
			}
		}

		context.setBody( co_await folly::coro::co_withExecutor( &executor, body.finish() ) );
		response = co_await routes.dispatch( std::move( match ), std::move( context ) );
	}
	catch ( const proxygen::coro::HTTPError& error )
	{
		const auto error_msg { std::format( "malformed request body for {} {}: {}", method_name, path, error.what() ) };
		spdlog::warn( "httpapi: {}", error_msg );
		response = Response::error( 400, error_msg );
	}
	catch ( const std::exception& error )
	{
		const auto error_msg { std::format( "unhandled exception in {} {}: {}", method_name, path, error.what() ) };
		spdlog::error( "httpapi: {}", error_msg );
		response = Response::error( 500, std::format( "Internal server error: {}", error_msg ) );
	}
	catch ( ... )
	{
		spdlog::error( "httpapi: unhandled non-standard exception in {} {}", method_name, path );
		response = Response::error( 500, "Internal server error (Nonstandard exception)" );
	}
	co_return makeResponseSource( std::move( response ), head );
}

[[nodiscard]] static proxygen::coro::HTTPSourceHolder rejectRequest(
	Response response,
	const bool http1,
	const bool head )
{
	// An HTTP/1 connection with unread body bytes must not be reused.
	if ( http1 ) response.message.getHeaders().set( "Connection", "close" );

	return makeResponseSource( std::move( response ), head );
}

HTTPServer::HTTPServer( HTTPConfig config, RouteRegistry routes ) :
  m_context {},
  m_config( std::move( config ) ),
  m_routes( std::move( routes ) ),
  m_drain {},
  m_server {},
  m_thread {},
  m_next_request_id( 1 ),
  m_port {},
  m_started( false )
{}

HTTPServer::HTTPServer( IDHANServerContext& context, HTTPConfig config, RouteRegistry routes ) :
  HTTPServer( std::move( config ), std::move( routes ) )
{
	m_context = &context;
}

HTTPServer::~HTTPServer()
{
	stop();
}

void HTTPServer::start()
{
	if ( m_started ) throw std::logic_error( "The HTTP server has already been started" );

	m_config.validate();
	std::shared_ptr< proxygen::coro::HTTPHandler > handler { this, []( proxygen::coro::HTTPHandler* ) {} };
	m_server.emplace(
		makeProxygenConfig( m_config ),
		std::make_shared< proxygen::coro::ExpectContinueWrapperHandler >( std::move( handler ) ) );
	folly::Baton<> started;
	std::exception_ptr start_error;
	m_thread = std::thread(
		[ this, &started, &start_error ]
		{
			try
			{
				m_server->start(
					[ &started ] noexcept { started.post(); },
					[ &started, &start_error ]( std::exception_ptr error ) noexcept
					{
						start_error = std::move( error );
						started.post();
					} );
			}
			catch ( ... )
			{
				start_error = std::current_exception();
				started.post();
			}
		} );
	started.wait();

	if ( start_error )
	{
		m_thread.join();
		m_server.reset();
		std::rethrow_exception( start_error );
	}

	const auto address { m_server->address() };
	if ( !address )
	{
		stop();
		throw std::runtime_error( "The HTTP server started without a bound address" );
	}

	m_port = address->getPort();
	m_started = true;
}

void HTTPServer::stopAdmission()
{
	m_drain.closeAdmission();
	if ( m_started ) m_server->drain();
}

HTTPTask<> HTTPServer::drain()
{
	stopAdmission();
	co_await m_drain.drain();
}

void HTTPServer::stop()
{
	if ( !m_thread.joinable() ) return;

	stopAdmission();
	m_server->forceStop();
	m_thread.join();
	m_server.reset();
	m_started = false;
}

std::uint16_t HTTPServer::port() const noexcept
{
	return m_port;
}

HTTPTask< proxygen::coro::HTTPSourceHolder > HTTPServer::handleRequest(
	[[maybe_unused]] folly::EventBase* event_base,
	[[maybe_unused]] proxygen::coro::HTTPSessionContextPtr session,
	proxygen::coro::HTTPSourceHolder source )
{
	auto headers { co_await folly::coro::co_awaitTry( source.readHeaderEvent() ) };
	if ( headers.hasException() || !headers->headers )
		co_return rejectRequest( Response::error( 400, "Malformed request" ), true, false );

	auto request { std::move( headers->headers ) };
	const bool http1 { request->getHTTPVersion().first < 2 };

	const auto method { request->getMethod() };
	if ( !method ) co_return rejectRequest( Response::error( 400, "Malformed request" ), http1, false );

	const bool head { *method == proxygen::HTTPMethod::HEAD };
	if ( request->getHeaders().exists( "Expect" ) )
		co_return rejectRequest( Response::error( 417, "Expectations are not supported" ), http1, head );

	auto query { RequestParameters::parseQuery( request->getQueryStringAsStringPiece() ) };
	if ( !query ) co_return rejectRequest( Response::error( 400, "Invalid query parameters" ), http1, head );

	const auto path_piece { request->getPathAsStringPiece() };
	const std::string_view path { path_piece.data(), path_piece.size() };
	const auto match { m_routes.match( *method, path ) };

	if ( *method == proxygen::HTTPMethod::OPTIONS && request->getHeaders().exists( "Origin" ) )
		co_return rejectRequest( preflightResponse( match ), http1, head );

	if ( match.status != RouteMatch::Status::found )
	{
		spdlog::debug(
			"Routing {} {} -> no match (status {})",
			proxygen::methodToString( *method ),
			path,
			static_cast< int >( match.status ) );
		co_return rejectRequest( routeError( match ), http1, head );
	}

	auto parameters { combineParameters( std::move( *query ), match.path_parameters ) };
	std::optional< Response > rejection;
	try
	{
		rejection = co_await m_routes.validateRequest( match, *request, parameters );
	}
	catch ( const std::exception& error )
	{
		const auto error_msg { std::format(
			"unhandled exception validating {} {}: {}", proxygen::methodToString( *method ), path, error.what() ) };
		spdlog::error( error_msg );
		rejection = Response::error( 500, error_msg );
	}
	catch ( ... )
	{
		const auto error_msg { std::format(
			"unhandled non-standard exception validating {} {}", proxygen::methodToString( *method ), path ) };
		spdlog::error( error_msg );
		rejection = Response::error( 500, error_msg );
	}
	if ( rejection ) co_return rejectRequest( std::move( *rejection ), http1, head );

	auto work { m_drain.admit() };
	if ( !work ) co_return rejectRequest( Response::error( 503, "Server is shutting down" ), http1, head );

	RequestContext context {
		m_context,
		m_next_request_id.fetch_add( 1 ),
		std::move( *request ),
		std::move( parameters ),
		folly::IOBuf::create( 0 ),
		std::move( *work )
	};

	auto response { readBodyAndDispatch(
		m_routes, match, std::move( context ), std::move( source ), headers->eom, m_config, m_body_executor ) };

	co_return co_await std::move( response );
}
} // namespace idhan::http
