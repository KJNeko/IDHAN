#include "APIAuth.hpp"

#include <expected>
#include <mutex>
#include <string_view>

#include "auth/authKeys.hpp"
#include "crypto/SHA256.hpp"
#include "helpers/createBadRequest.hpp"

namespace idhan::api
{

static constexpr std::array< std::string_view, 5 > key_headers {
	{ "Authorization", "X-API-Key", "IDHAN-API-Key", "X-Api-Key", "IDHAN-Api-Key" }
};

//! Checks all of the common headers that we'll use for the idhan key
static std::string getHeaderKeys( const drogon::HttpRequestPtr& req )
{
	for ( const auto& header : key_headers )
	{
		const auto header_data { req->getHeader( std::string( header ) ) };

		if ( !header_data.empty() ) return header_data;
	}

	return "";
}

static std::expected< SHA256, drogon::HttpResponsePtr > getAndValidateKey( const drogon::HttpRequestPtr& req )
{
	const auto idhan_key_param { req->getOptionalParameter< std::string >( "idhan_key" ) };

	const auto idhan_key_header { getHeaderKeys( req ) };

	if ( idhan_key_header.empty() && !idhan_key_param )
	{
		return std::unexpected( createBadRequest( "idhan_key parameter not provided in parameters or header" ) );
	}

	const auto key { idhan_key_param ? *idhan_key_param : idhan_key_header };

	if ( key.size() != 64 )
	{
		return std::unexpected( createBadRequest( "Invalid key length for idhan_key. Expected 64 characters in hex" ) );
	}

	auto sha256_key { SHA256::fromHex( key ) };

	if ( !sha256_key ) return std::unexpected( sha256_key.error() );

	return *sha256_key;
}

drogon::Task< drogon::HttpResponsePtr > APIAuth::doFilter( const drogon::HttpRequestPtr& req )
{
#ifdef IDHAN_DISABLE_API_AUTH
	static std::once_flag auth_disabled_flag;
	std::call_once( auth_disabled_flag, [] { log::warn( "!!! API Auth Disabled. Approving all requests !!!" ); } );
	co_return nullptr;
#else
	auto key_res { getAndValidateKey( req ) };

	if ( !key_res ) co_return key_res.error();

	auto& sha256_key { *key_res };

	auto db { drogon::app().getDbClient() };

	const bool key_exists { co_await auth::keyExists( sha256_key, db ) };

	if ( !key_exists )
	{
		auto response { drogon::HttpResponse::newHttpResponse() };
		log::warn( "Invalid API key given!" );
		response->setStatusCode( drogon::k401Unauthorized );
		response->setBody( "Invalid API Key" );
		co_return response;
	}

	co_return nullptr;
#endif
}

drogon::Task< drogon::HttpResponsePtr > AuthEndpoint::verifyAccessKey( drogon::HttpRequestPtr req )
{
#ifdef IDHAN_DISABLE_API_AUTH
	log::warn( "!!! API Auth Disabled. Approving all requests !!!" );
	Json::Value out_json {};
	out_json[ "success" ] = true;
	out_json[ "message" ] = "Key is valid (Auth Disabled)";
	co_return drogon::HttpResponse::newHttpJsonResponse( out_json );
#else
	auto key_res { getAndValidateKey( req ) };

	if ( !key_res ) co_return key_res.error();

	auto& sha256_key { *key_res };

	auto db { drogon::app().getDbClient() };

	const bool key_exists { co_await auth::keyExists( sha256_key, db ) };

	Json::Value out_json {};

	if ( !key_exists )
	{
		out_json[ "success" ] = false;
		out_json[ "message" ] = "Invalid API Key";
		auto response { drogon::HttpResponse::newHttpJsonResponse( out_json ) };
		response->setStatusCode( drogon::k401Unauthorized );
		co_return response;
	}

	out_json[ "success" ] = true;
	out_json[ "message" ] = "Key is valid";
	co_return drogon::HttpResponse::newHttpJsonResponse( out_json );
#endif
}

drogon::Task< drogon::HttpResponsePtr > AuthEndpoint::generateApiKey( [[maybe_unused]] drogon::HttpRequestPtr req )
{
	auto db { drogon::app().getDbClient() };

	const auto key_count { co_await auth::keyCount( db ) };

	if ( key_count > 0 )
	{
		co_return createConflict( "An API key already exists. Use the existing key to manage keys." );
	}

	const auto key_gen {
		co_await db->execSqlCoro( "INSERT INTO auth_keys (key_hash) VALUES (gen_random_bytes(32)) RETURNING key_hash" )
	};

	const SHA256 key { SHA256::fromPgCol( key_gen[ 0 ][ 0 ] ) };

	Json::Value value {};
	value[ "key" ] = key.hex();

	co_return drogon::HttpResponse::newHttpJsonResponse( value );
}

drogon::Task< drogon::HttpResponsePtr > AuthEndpoint::verifyKey( [[maybe_unused]] drogon::HttpRequestPtr req )
{
	Json::Value out {};
	out[ "authenticated" ] = true;
	co_return drogon::HttpResponse::newHttpJsonResponse( out );
}

} // namespace idhan::api
