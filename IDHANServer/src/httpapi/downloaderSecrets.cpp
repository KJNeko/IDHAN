#include "downloaderSecrets.hpp"

#include <format>
#include <simdjson.h>
#include <string>
#include <string_view>
#include <unordered_map>

#include "IDHANDownloader/secrets/SecretDomain.hpp"
#include "downloader/DownloadSessionManager.hpp"
#include "idhan/db/DBContext.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"

namespace idhan::httpapi
{

[[nodiscard]] static std::expected< DownloaderSecrets, std::string > decodeStoredSecrets(
	const std::unordered_map< std::string, std::string >& secrets )
{
	DownloaderSecrets document;
	simdjson::dom::parser parser;
	for ( const auto& [ name, value ] : secrets )
	{
		simdjson::dom::element object;
		const auto error { parser.parse( value ).get( object ) };
		if ( error != simdjson::SUCCESS ) return std::unexpected( "Stored downloader secrets contain invalid JSON" );

		auto values_e { http::decodeJson< DownloaderJsonObject >( object ) };
		if ( !values_e.has_value() ) return std::unexpected( values_e.error().message );

		document.emplace( name, std::move( values_e.value() ) );
	}

	return document;
}

[[nodiscard]] static http::JsonResult< DownloaderSecrets > secretsResponse(
	const std::unordered_map< std::string, std::string >& secrets )
{
	auto document_e { decodeStoredSecrets( secrets ) };
	if ( !document_e.has_value() )
	{
		auto response { http::Response::error( 500, document_e.error() ) };
		response.message.getHeaders().set( "Cache-Control", "no-store" );
		return response;
	}

	http::JsonResponse< DownloaderSecrets > response { std::move( document_e.value() ) };
	response.headers.set( "Cache-Control", "no-store" );
	return response;
}

http::HTTPTask< http::JsonResult< DownloaderSecrets > > listSecrets( [[maybe_unused]] http::RequestContext context )
{
	const auto secrets { co_await downloader::downloadSessionManager().secrets() };
	if ( !secrets )
		co_return http::Response::error(
			500, std::format( "Unable to fetch downloader secrets: {}", secrets.error() ) );

	co_return secretsResponse( *secrets );
}

http::HTTPTask< http::JsonResult< DownloaderSecrets > > setSecrets( http::JsonBody< DownloaderSecrets > body )
{
	std::unordered_map< std::string, std::string > values;
	for ( const auto& [ name, object ] : body.value )
	{
		const auto domain_e { downloader::secretDomain( name ) };
		if ( !domain_e.has_value() ) co_return http::Response::error( 400, domain_e.error() );

		auto text_e { http::serializeJson( object ) };
		if ( !text_e.has_value() ) co_return http::Response::error( 400, text_e.error().message );

		if ( !values.emplace( domain_e.value(), std::move( text_e.value() ) ).second )
			co_return http::Response::error( 400, "Duplicate normalized secret domain" );
	}

	const auto updated { co_await downloader::downloadSessionManager().setSecrets( std::move( values ) ) };
	if ( !updated )
		co_return http::Response::error( 500, std::format( "Unable to set downloader secrets: {}", updated.error() ) );

	const auto secrets { co_await downloader::downloadSessionManager().secrets() };
	if ( !secrets )
		co_return http::Response::error(
			500, std::format( "Unable to fetch downloader secrets: {}", secrets.error() ) );

	co_return secretsResponse( *secrets );
}

} // namespace idhan::httpapi
