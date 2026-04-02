//
// Created by kj16609 on 11/8/24.
//

#include "APIAuth.hpp"

#include <expected>

#include "crypto/SHA256.hpp"
#include "helpers/createBadRequest.hpp"

namespace idhan::api
{

static constexpr std::array< std::string, 5 > key_headers {
	{ "Authorization", "X-API-Key", "IDHAN-API-Key", "X-Api-Key", "IDHAN-Api-Key" }
};

//! Checks all of the common headers that we'll use for the idhan key
std::string getHeaderKeys( const drogon::HttpRequestPtr& req )
{
	for ( const auto& header : key_headers )
	{
		const auto header_data { req->getHeader( header ) };

		if ( !header_data.empty() ) return header_data;
	}

	return "";
}

static std::expected< SHA256, drogon::HttpResponsePtr > getAndValidateKey( const drogon::HttpRequestPtr& req )
{
	// check if there is a `idhan_key` parameter
	const auto idhan_key_param { req->getOptionalParameter< std::string >( "idhan_key" ) };

	const auto idhan_key_header { getHeaderKeys( req ) };

	if ( idhan_key_header.empty() && !idhan_key_param )
	{
		return std::unexpected( createBadRequest( "idhan_key parameter not provided in parameters or header" ) );
	}

	const auto key { idhan_key_param ? *idhan_key_param : idhan_key_header };

	// the key should be 64 characters because it's in hex
	if ( key.size() != 64 )
	{
		return std::unexpected( createBadRequest( "Invalid key length for idhan_key. Expected 64 characters in hex" ) );
	}

	// got the key
	auto sha256_key { SHA256::fromHex( key ) };

	if ( !sha256_key ) return std::unexpected( sha256_key.error() );

	return *sha256_key;
}

drogon::Task< drogon::HttpResponsePtr > APIAuth::doFilter( const drogon::HttpRequestPtr& req )
{
	// is there a cookie for us?
	const auto idhan_key_session { req->getCookie( "idhan_key_session" ) };

	auto key_res { getAndValidateKey( req ) };

	if ( !key_res ) co_return key_res.error();

	auto& sha256_key { *key_res };

	auto db { drogon::app().getDbClient() };

	const auto select_key {
		co_await db->execSqlCoro( "SELECT key_id FROM auth_keys WHERE key_hash = $1", sha256_key.toVec() )
	};

	if ( select_key.empty() )
	{
		// return permission denied
		auto response { drogon::HttpResponse::newHttpResponse() };
		log::warn( "Invalid API key given!" );
		response->setStatusCode( drogon::k401Unauthorized );
		response->setBody( "Invalid API Key" );
		co_return response;
	}

	// do nothing. filter passed
	co_return nullptr;
}

drogon::Task< drogon::HttpResponsePtr > AuthEndpoint::verifyAccessKey( drogon::HttpRequestPtr req )
{
	auto key_res { getAndValidateKey( req ) };

	if ( !key_res ) co_return key_res.error();

	auto& sha256_key { *key_res };

	auto db { drogon::app().getDbClient() };

	const auto select_key {
		co_await db->execSqlCoro( "SELECT key_id FROM auth_keys WHERE key_hash = $1", sha256_key.toVec() )
	};

	Json::Value out_json {};

	if ( select_key.empty() )
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
}

drogon::Task< drogon::HttpResponsePtr > AuthEndpoint::generateApiKey( [[maybe_unused]] drogon::HttpRequestPtr req )
{
	auto db { drogon::app().getDbClient() };

	const auto key_count_search { co_await db->execSqlCoro( "SELECT count(*) FROM auth_keys" ) };

	if ( !key_count_search.empty() && key_count_search[ 0 ][ 0 ].as< std::size_t >() > 0 )
	{
		co_return drogon::HttpResponsePtr {};
	}

	const auto key_gen {
		co_await db->execSqlCoro( "INSERT INTO auth_keys (key_hash) VALUES (gen_random_bytes(32)) RETURNING key_hash" )
	};

	const SHA256 key { SHA256::fromPgCol( key_gen[ 0 ][ 0 ] ) };

	Json::Value value {};
	value[ "key" ] = key.hex();

	co_return drogon::HttpResponse::newHttpJsonResponse( value );
}

} // namespace idhan::api
