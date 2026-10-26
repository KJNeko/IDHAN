#include "downloaderSecrets.hpp"

#include <folly/io/IOBuf.h>

#include <format>
#include <simdjson.h>
#include <string>
#include <string_view>
#include <unordered_map>

#include "apiAuth.hpp"
#include "downloader/DownloadSessionManager.hpp"
#include "idhan/db/DBContext.hpp"
#include "jsonBody.hpp"

namespace idhan::httpapi
{

[[nodiscard]] static http::Response secretsResponse( const std::unordered_map< std::string, std::string >& secrets )
{
	simdjson::builder::string_builder builder;
	builder.start_object();

	bool first { true };
	for ( const auto& [ name, value ] : secrets )
	{
		if ( !first ) builder.append_comma();
		first = false;
		builder.append_key_value( name, value );
	}

	builder.end_object();

	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	auto response { http::Response::json( 200, folly::IOBuf::copyBuffer( *json ) ) };
	response.message.getHeaders().set( "Cache-Control", "no-store" );
	return response;
}

static http::HTTPTask< http::Response > listSecrets( [[maybe_unused]] http::RequestContext context )
{
	const auto secrets { co_await downloader::downloadSessionManager().secrets() };
	if ( !secrets )
		co_return http::Response::error(
			500, std::format( "Unable to fetch downloader secrets: {}", secrets.error() ) );

	co_return secretsResponse( *secrets );
}

static http::HTTPTask< http::Response > setSecrets( http::RequestContext context )
{
	auto body_e { readValidatedJsonBody( context ) };
	if ( !body_e ) co_return std::move( body_e.error() );

	std::unordered_map< std::string, std::string > values {};

	simdjson::dom::parser parser;
	try
	{
		const simdjson::dom::element root { parser.parse( *body_e ) };
		simdjson::dom::object object {};
		if ( !root.is_object() || root.get_object().get( object ) != simdjson::SUCCESS )
			co_return http::Response::error(
				400, "Request body must be a JSON object mapping secret keys to string values" );

		for ( const simdjson::dom::key_value_pair field : object )
		{
			if ( field.key.empty() ) co_return http::Response::error( 400, "Downloader secret keys must not be empty" );

			std::string_view text {};
			if ( field.value.get_string().get( text ) != simdjson::SUCCESS )
				co_return http::Response::error(
					400, std::format( "Downloader secret '{}' must have a string value", field.key ) );

			values.emplace( std::string { field.key }, std::string { text } );
		}
	}
	catch ( const simdjson::simdjson_error& e )
	{
		co_return http::Response::error( 400, e.what() );
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

http::RouteDefinition downloaderSecretsListRoute( db::DBContext& db )
{
	return http::RouteDefinition {
		http::RouteMetadata { proxygen::HTTPMethod::GET, "/downloader/secrets", {}, { apiAuthValidation( db ) } },
		listSecrets
	};
}

http::RouteDefinition downloaderSecretsSetRoute( db::DBContext& db )
{
	return http::RouteDefinition {
		http::RouteMetadata { proxygen::HTTPMethod::POST, "/downloader/secrets", {}, { apiAuthValidation( db ) } },
		setSecrets
	};
}

} // namespace idhan::httpapi
