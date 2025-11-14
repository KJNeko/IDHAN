//
// Created by kj16609 on 6/12/25.
//

#include "IDHANTypes.hpp"
#include "api/RecordAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "crypto/SHA256.hpp"
#include "fgl/defines.hpp"
#include "metadata/metadata.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > RecordAPI::fetchInfo(
	[[maybe_unused]] drogon::HttpRequestPtr request,
	RecordID record_id )
{
	const auto db { drogon::app().getDbClient() };

	Json::Value root {};
	root[ "record_id" ] = record_id;

	const auto record_info { co_await db->execSqlCoro( "SELECT * FROM records WHERE record_id = $1", record_id ) };

	if ( record_info.empty() ) co_return createBadRequest( "Record ID was not found" );

	root[ "hashes" ][ "sha256" ] = SHA256::fromPgCol( record_info[ 0 ][ "sha256" ] ).hex();

	const auto file_info { co_await db->execSqlCoro( "SELECT * FROM file_info WHERE record_id = $1", record_id ) };

	if ( !file_info.empty() && !file_info[ 0 ][ "mime_id" ].isNull() )
	{
		root[ "size" ] = file_info[ 0 ][ "size" ].as< std::size_t >();

		const auto mime_info { co_await db->execSqlCoro(
			"SELECT * FROM mime WHERE mime_id = $1", file_info[ 0 ][ "mime_id" ].as< MimeID >() ) };

		root[ "mime" ] = mime_info[ 0 ][ "name" ].as< std::string >();
		root[ "extension" ] = mime_info[ 0 ][ "best_extension" ].as< std::string >();

		co_await metadata::addFileSpecificInfo( root, record_id, db );
	}

	co_return drogon::HttpResponse::newHttpJsonResponse( root );
}

drogon::Task< drogon::HttpResponsePtr > RecordAPI::parseFile( drogon::HttpRequestPtr request, RecordID record_id )
{
	const auto db { drogon::app().getDbClient() };
	const auto parse_result { co_await metadata::tryParseRecordMetadata( record_id, db ) };
	if ( !parse_result ) co_return parse_result.error();

	co_return co_await fetchInfo( request, record_id );
}

} // namespace idhan::api
