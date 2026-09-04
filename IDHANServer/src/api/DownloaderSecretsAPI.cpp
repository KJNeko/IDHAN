#include "DownloaderSecretsAPI.hpp"

#include <json/json.h>

#include <string>
#include <unordered_map>

#include "api/helpers/createBadRequest.hpp"
#include "downloader/DownloadSessionManager.hpp"

namespace idhan::api
{

static drogon::HttpResponsePtr secretResponse( const std::unordered_map< std::string, std::string >& secrets )
{
	Json::Value json { Json::objectValue };

	for ( const auto& [ name, value ] : secrets ) json[ name ] = value;

	auto response { drogon::HttpResponse::newHttpJsonResponse( std::move( json ) ) };
	response->addHeader( "Cache-Control", "no-store" );
	return response;
}

drogon::Task< drogon::HttpResponsePtr > DownloaderSecretsAPI::list( drogon::HttpRequestPtr )
{
	const auto secrets { co_await downloader::downloadSessionManager().secrets() };

	if ( !secrets ) co_return createInternalError( "Unable to fetch downloader secrets: {}", secrets.error() );

	co_return secretResponse( *secrets );
}

drogon::Task< drogon::HttpResponsePtr > DownloaderSecretsAPI::set( drogon::HttpRequestPtr request )
{
	const auto body { request->getJsonObject() };

	if ( body == nullptr || !body->isObject() )
		co_return createBadRequest( "Request body must be a JSON object mapping secret keys to string values" );

	std::unordered_map< std::string, std::string > values {};

	for ( const auto& name : body->getMemberNames() )
	{
		if ( name.empty() ) co_return createBadRequest( "Downloader secret keys must not be empty" );

		if ( !( *body )[ name ].isString() )
			co_return createBadRequest( "Downloader secret '{}' must have a string value", name );

		values.emplace( name, ( *body )[ name ].asString() );
	}

	const auto updated { co_await downloader::downloadSessionManager().setSecrets( std::move( values ) ) };

	if ( !updated ) co_return createInternalError( "Unable to set downloader secrets: {}", updated.error() );

	const auto secrets { co_await downloader::downloadSessionManager().secrets() };

	if ( !secrets ) co_return createInternalError( "Unable to fetch downloader secrets: {}", secrets.error() );

	co_return secretResponse( *secrets );
}

} // namespace idhan::api
