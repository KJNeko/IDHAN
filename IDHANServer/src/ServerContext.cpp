//
// Created by kj16609 on 7/23/24.
//

#include "ServerContext.hpp"

#include <fixme.hpp>

#include "core/Database.hpp"
#include "drogon/HttpAppFramework.h"
#include "hyapi/setups.hpp"
#include "logging/log.hpp"

constexpr std::uint16_t DEFAULT_PORT { 16609 };

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

		drogon::app()
			.setLogPath( "./" )
			.setLogLevel( trantor::Logger::kInfo )
			.addListener( "127.0.0.1", DEFAULT_PORT )
			.setThreadNum( 16 );

		setupCORSSupport();

		hyapi::setupAccessHandlers();
		hyapi::setupServiceHandlers();

		log::server::info( "IDHAN initalization finished" );
	}

	void ServerContext::run()
	{
		log::server::info( "Starting runtime" );

		drogon::app().run();

		log::server::info( "Shutting down" );
	}

	ServerContext::~ServerContext()
	{}

} // namespace idhan
