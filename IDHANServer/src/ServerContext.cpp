//
// Created by kj16609 on 7/23/24.
//

#include "ServerContext.hpp"

#include "ConnectionArguments.hpp"
#include "NET_CONSTANTS.hpp"
#include "api/helpers/ResponseCallback.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "db/setup/ManagementConnection.hpp"
#include "drogon/HttpAppFramework.h"
#include "logging/log.hpp"

namespace idhan
{

void ServerContext::setupCORSSupport()
{
	drogon::app().registerPreRoutingAdvice(
		[]( const drogon::HttpRequestPtr& request, drogon::FilterCallback&& stop, drogon::FilterChainCallback&& pass )
		{
			log::debug( "{}:{}", request->getMethodString(), request->getPath() );

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

void exceptionHandler( const std::exception& e, const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
{
	log::error( "Unhandled exception got to drogon! In request: {} What: {}", request->getQuery(), e.what() );
	spdlog::dump_backtrace();

	auto response { idhan::createInternalError(
		"Unhandled exception got to drogon! In request: {} What: {}", request->getPath(), e.what() ) };

	callback( response );
	// drogon::defaultExceptionHandler( e, request, std::move( callback ) );
}

ServerContext::ServerContext( const ConnectionArguments& arguments ) :
  m_postgresql_management( std::make_unique< ManagementConnection >( arguments ) )
{
	log::server::info( "IDHAN initalization starting" );

	auto& app { drogon::app() };

	spdlog::enable_backtrace( 32 );

	log::debug( "Logging show debug" );
	log::info( "Logging show info" );

	drogon::app()
		.setLogPath( "./" )
		.setLogLevel( trantor::Logger::kInfo )
		.addListener( "127.0.0.1", IDHAN_DEFAULT_PORT )
		.setThreadNum( 32 )
		.setClientMaxBodySize( std::numeric_limits< std::uint64_t >::max() )
		.setDocumentRoot( "./pages" )
		.setExceptionHandler( exceptionHandler );

	drogon::orm::PostgresConfig config;
	config.host = arguments.hostname;
	config.port = arguments.port;
	config.databaseName = arguments.dbname;
	config.username = arguments.user;
	config.password = arguments.password;
	config.connectionNumber = std::thread::hardware_concurrency();
	config.name = "default";
	config.isFast = false;
	config.characterSet = "UTF-8";
	config.timeout = 60.0f;
	config.autoBatch = false;

	if ( arguments.testmode )
	{
		config.connectOptions.emplace( std::make_pair( "searchpatch", "test" ) );
	}

	drogon::app().addDbClient( config );

	setupCORSSupport();

	log::server::info( "IDHAN initialization finished" );
}

void trantorHook( const char* msg, const std::uint64_t len )
{
	log::info( "Hook: {}", std::string_view( msg, len ) );
}

void ServerContext::run()
{
	log::server::info( "Starting runtime" );

#ifndef NDEBUG
	trantor::Logger::setOutputFunction( trantorHook, []() {} );
#endif

	log::info( "Server available at http://localhost:{}", IDHAN_DEFAULT_PORT );
	log::info( "Swagger docs available at http://localhost:{}/api", IDHAN_DEFAULT_PORT );

	drogon::app().run();

	// log::server::info( "Shutting down" );
	return;
}

ServerContext::~ServerContext()
{}

} // namespace idhan
