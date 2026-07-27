//
// Created by kj16609 on 11/8/24.
//

#include "APIAuth.hpp"

#include <expected>
#include <mutex>
#include <string_view>

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
#ifdef IDHAN_DISABLE_API_AUTH
	static std::once_flag auth_disabled_flag;
	std::call_once( auth_disabled_flag, [] { log::warn( "!!! API Auth Disabled. Approving all requests !!!" ); } );
	co_return nullptr;
#else
	auto key_res { getAndValidateKey( req ) };

	if ( !key_res ) co_return key_res.error();

	auto& sha256_key { *key_res };

	auto db { drogon::app().getDbClient() };

	// The presented key may be either a permanent API key or a temporary session key; they are
	// indistinguishable at this point (both 32-byte blobs), so accept either. Expired sessions do
	// not match.
	const auto select_key { co_await db->execSqlCoro(
		"SELECT key_id FROM auth_keys WHERE key_hash = $1 "
		"UNION ALL "
		"SELECT key_id FROM auth_sessions WHERE session_key = $1 AND expires_at > now() "
		"LIMIT 1",
		sha256_key.toVec() ) };

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
#endif
}

drogon::Task< drogon::HttpResponsePtr > AuthEndpoint::generateApiKey( [[maybe_unused]] drogon::HttpRequestPtr req )
{
	auto db { drogon::app().getDbClient() };

	const auto key_count_search { co_await db->execSqlCoro( "SELECT count(*) FROM auth_keys" ) };

	if ( !key_count_search.empty() && key_count_search[ 0 ][ 0 ].as< int64_t >() > 0 )
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

//! How long a freshly minted session key remains valid.
[[maybe_unused]] static constexpr int session_lifetime_days { 30 };

drogon::Task< drogon::HttpResponsePtr > AuthEndpoint::createSession( [[maybe_unused]] drogon::HttpRequestPtr req )
{
#ifdef IDHAN_DISABLE_API_AUTH
	// Auth is disabled, so the filter approves everything and the returned key is never actually
	// checked. Hand back a placeholder so the client has something to store and its normal flow
	// works unchanged.
	log::warn( "!!! API Auth Disabled. Issuing a placeholder session key !!!" );
	Json::Value out {};
	out[ "session_key" ] = std::string( 64, '0' );
	out[ "expires_at" ] = 0;
	co_return drogon::HttpResponse::newHttpJsonResponse( out );
#else
	auto key_res { getAndValidateKey( req ) };

	if ( !key_res ) co_return key_res.error();

	auto& sha256_key { *key_res };

	auto db { drogon::app().getDbClient() };

	// Only a permanent API key may mint a session — deliberately not auth_sessions, so a session
	// key cannot be used to spawn further sessions and extend itself indefinitely.
	const auto select_key {
		co_await db->execSqlCoro( "SELECT key_id FROM auth_keys WHERE key_hash = $1", sha256_key.toVec() )
	};

	if ( select_key.empty() )
	{
		auto response { drogon::HttpResponse::newHttpResponse() };
		log::warn( "Session requested with an invalid API key!" );
		response->setStatusCode( drogon::k401Unauthorized );
		response->setBody( "Invalid API Key" );
		co_return response;
	}

	const auto key_id { select_key[ 0 ][ 0 ].as< std::int32_t >() };

	const auto session_gen { co_await db->execSqlCoro(
		"INSERT INTO auth_sessions (session_key, key_id, expires_at) "
		"VALUES (gen_random_bytes(32), $1, now() + make_interval(days => $2)) "
		"RETURNING session_key, extract(epoch FROM expires_at)::bigint",
		key_id,
		session_lifetime_days ) };

	const SHA256 session_key { SHA256::fromPgCol( session_gen[ 0 ][ 0 ] ) };

	Json::Value out {};
	out[ "session_key" ] = session_key.hex();
	out[ "expires_at" ] = session_gen[ 0 ][ 1 ].as< std::int64_t >();

	co_return drogon::HttpResponse::newHttpJsonResponse( out );
#endif
}

drogon::Task< drogon::HttpResponsePtr > AuthEndpoint::checkSession( [[maybe_unused]] drogon::HttpRequestPtr req )
{
	// Reaching this handler means the auth filter already accepted the key.
	Json::Value out {};
	out[ "authenticated" ] = true;
	co_return drogon::HttpResponse::newHttpJsonResponse( out );
}

drogon::Task< drogon::HttpResponsePtr > AuthEndpoint::deleteSession( [[maybe_unused]] drogon::HttpRequestPtr req )
{
	Json::Value out {};

#ifdef IDHAN_DISABLE_API_AUTH
	out[ "revoked" ] = false;
	co_return drogon::HttpResponse::newHttpJsonResponse( out );
#else
	auto key_res { getAndValidateKey( req ) };

	if ( !key_res ) co_return key_res.error();

	auto& sha256_key { *key_res };

	auto db { drogon::app().getDbClient() };

	// Deletes only if the presented key is a session key; a permanent key matches nothing here.
	const auto deleted {
		co_await db->execSqlCoro( "DELETE FROM auth_sessions WHERE session_key = $1", sha256_key.toVec() )
	};

	out[ "revoked" ] = deleted.affectedRows() > 0;
	co_return drogon::HttpResponse::newHttpJsonResponse( out );
#endif
}

} // namespace idhan::api
