//
// Created by kj16609 on 11/8/24.
//

#include "APIAuth.hpp"

#include "crypto/SHA256.hpp"
#include "helpers/createBadRequest.hpp"

namespace idhan::api
{

static constexpr std::array< std::string, 3 > key_headers { { "Authorization", "X-API-Key", "IDHAN-API-Key" } };

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

drogon::Task< drogon::HttpResponsePtr > APIAuth::doFilter( const drogon::HttpRequestPtr& req )
{
	// is there a cookie for us?
	const auto idhan_key_session { req->getCookie( "idhan_key_session" ) };
	if ( idhan_key_session != "" )
	{
		// yes.
	}

	// check if there is a `idhan_key` parameter
	const auto idhan_key_param { req->getOptionalParameter< std::string >( "idhan_key" ) };

	const auto idhan_key_header { getHeaderKeys( req ) };

	if ( idhan_key_header == "" && !idhan_key_param )
	{
		co_return createBadRequest( "idhan_key parameter not provided in parameters or header" );
	}

	const auto key { idhan_key_param ? *idhan_key_param : idhan_key_header };

	// the key should be 64 characters because it's in hex
	if ( key.size() != 64 )
	{
		co_return createBadRequest( "Invalid key length for idhan_key. Expected 64 characters in hex" );
	}

	// got the key
	auto sha256_key { SHA256::fromHex( key ) };

	if ( !sha256_key ) co_return sha256_key.error();

	auto db { drogon::app().getFastDbClient() };

	const auto select_key {
		co_await db->execSqlCoro( "SELECT key_id FROM auth_keys WHERE key_hash = $1", sha256_key->toVec() )
	};

	if ( select_key.empty() )
	{
		// return permission denied
		auto response { drogon::HttpResponse::newHttpResponse() };
		response->setStatusCode( drogon::k401Unauthorized );
		response->setBody( "Invalid API Key" );
		co_return response;
	}

	// do nothing. filter passed
	co_return nullptr;
}

drogon::Task< drogon::HttpResponsePtr > AuthEndpoint::verifyAccessKey( [[maybe_unused]] drogon::HttpRequestPtr req )
{
	co_return drogon::HttpResponse::newHttpResponse();
}

} // namespace idhan::api
