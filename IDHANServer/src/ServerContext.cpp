//
// Created by kj16609 on 7/23/24.
//

#include "ServerContext.hpp"

#include <fixme.hpp>

#include "NET_CONSTANTS.hpp"
#include "api/api.hpp"
#include "core/Database.hpp"
#include "drogon/HttpAppFramework.h"
#include "logging/log.hpp"

namespace idhan
{

	void ServerContext::setupCORSSupport()
	{
		drogon::app().registerPreRoutingAdvice(
			[]( const drogon::HttpRequestPtr& request,
		        drogon::FilterCallback&& stop,
		        drogon::FilterChainCallback&& pass )
			{
				if ( !request->path().starts_with( "/hyapi" ) || request->method() != drogon::Options )
				{
					pass();
					return;
				}

				auto response { drogon::HttpResponse::newHttpResponse() };

				response->addHeader( "Access-Control-Allow-Headers", "*" );
				response->addHeader( "Access-Control-Allow-Origin", "*" );
				response->addHeader( "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS, HEAD" );

				stop( response );
			} );

		drogon::app().registerPostHandlingAdvice(
			[]( [[maybe_unused]] const drogon::HttpRequestPtr& request, const drogon::HttpResponsePtr& response )
			{
				response->addHeader( "Access-Control-Allow-Headers", "*" );
				response->addHeader( "Access-Control-Allow-Origin", "*" );
				response->addHeader( "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS, HEAD" );
			} );
	}

	ServerContext::ServerContext( const ConnectionArguments& arguments ) :
	  m_db( std::make_unique< Database >( arguments ) )
	{
		log::server::info( "IDHAN initalization starting" );

		auto& app { drogon::app() };

		spdlog::set_level( spdlog::level::debug );
		log::trace( "Logging show trace" );
		log::debug( "Logging show debug" );
		log::info( "Logging show info" );

		drogon::app()
			.setLogPath( "./" )
			.setLogLevel( trantor::Logger::kInfo )
			.addListener( "127.0.0.1", IDHAN_DEFAULT_PORT )
			.setThreadNum( 16 )
			.setClientMaxBodySize( std::numeric_limits< std::size_t >::max() )
			.setDocumentRoot( "./pages" );

		setupCORSSupport();

		api::registerApi();

		log::server::info( "IDHAN initalization finished" );
	}

	void ServerContext::run()
	{
		log::server::info( "Starting runtime" );

		log::info( "Server available at http://localhost:{}", IDHAN_DEFAULT_PORT );
		drogon::app().run();

		log::server::info( "Shutting down" );
	}

	ServerContext::~ServerContext()
	{}

} // namespace idhan
