//
// Created by kj16609 on 6/12/25.
//

#include "crypto/SHA256.hpp"
#include "IDHANTypes.hpp"
#include "api/RecordAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
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

	const auto result { co_await db->execSqlCoro(
		"SELECT r.sha256, fi.size, fi.mime_id, m.name, m.best_extension "
		"FROM records r "
		"LEFT JOIN file_info fi ON fi.record_id = r.record_id "
		"LEFT JOIN mime m ON m.mime_id = fi.mime_id "
		"WHERE r.record_id = $1",
		record_id ) };

	if ( result.empty() ) co_return createNotFound( "Record ID was not found" );

	root[ "hashes" ][ "sha256" ] = SHA256::fromPgCol( result[ 0 ][ "sha256" ] ).hex();

	if ( !result[ 0 ][ "mime_id" ].isNull() )
	{
		root[ "size" ] = result[ 0 ][ "size" ].as< std::size_t >();
		root[ "mime" ] = result[ 0 ][ "name" ].as< std::string >();
		root[ "extension" ] = result[ 0 ][ "best_extension" ].as< std::string >();

		const auto metadata_result { co_await metadata::addFileSpecificInfo( root, record_id, db ) };
		if ( !metadata_result ) co_return metadata_result.error();
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
