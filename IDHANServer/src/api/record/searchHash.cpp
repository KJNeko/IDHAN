//
// Created by kj16609 on 3/11/25.
//

#include "api/RecordAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "crypto/SHA256.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > RecordAPI::searchHash( [[maybe_unused]] const drogon::HttpRequestPtr request )
{
	const auto hash_str { request->getParameter( "sha256" ) };

	if ( hash_str.empty() ) co_return createBadRequest( "sha256 was not provided in query" );

	constexpr std::size_t expected_hash_size { ( 256 / 8 ) * 2 };
	if ( hash_str.size() != expected_hash_size )
		co_return createBadRequest( "Hash size was invalid, must be {}", expected_hash_size );

	const auto expected_hash { SHA256::fromHex( hash_str ) };
	if ( !expected_hash ) co_return expected_hash.error();

	const auto& hash { expected_hash.value() };

	const auto db { drogon::app().getDbClient() };

	const auto result { co_await db->execSqlCoro( "SELECT record_id FROM records WHERE sha256 = $1", hash.toVec() ) };

	Json::Value json {};

	if ( result.empty() )
	{
		json[ "found" ] = false;
		co_return drogon::HttpResponse::newHttpJsonResponse( json );
	}

	json[ "found" ] = true;
	json[ "record_id" ] = result[ 0 ][ 0 ].as< std::size_t >();
	co_return drogon::HttpResponse::newHttpJsonResponse( json );
}

} // namespace idhan::api
