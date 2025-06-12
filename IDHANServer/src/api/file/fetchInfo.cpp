//
// Created by kj16609 on 6/12/25.
//

#include "IDHANTypes.hpp"
#include "api/IDHANRecordAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "crypto/SHA256.hpp"
#include "metadata/parseMetadata.hpp"

namespace idhan::api
{

drogon::Task< void > addImageInfo( Json::Value& root, const RecordID record_id, drogon::orm::DbClientPtr db )
{
	const auto metadata { co_await db->execSqlCoro( "SELECT * FROM image_metadata WHERE record_id = $1", record_id ) };

	if ( metadata.empty() ) co_return;

	root[ "width" ] = metadata[ 0 ][ "width" ].as< std::uint32_t >();
	root[ "height" ] = metadata[ 0 ][ "height" ].as< std::uint32_t >();
	root[ "channels" ] = metadata[ 0 ][ "channels" ].as< std::uint32_t >();
}

drogon::Task< void > addFileSpecificInfo(
	Json::Value& root,
	const drogon::orm::Result::const_reference& row,
	const RecordID record_id,
	drogon::orm::DbClientPtr db )
{
	const auto metadata {
		co_await db->execSqlCoro( "SELECT simple_mime_type FROM metadata WHERE record_id = $1", record_id )
	};

	if ( metadata.empty() ) co_return;

	const SimpleMimeType simple_mime_type { metadata[ 0 ][ "simple_mime_type" ].as< std::uint16_t >() };

	switch ( simple_mime_type )
	{
		case SimpleMimeType::IMAGE:
			co_await addImageInfo( root, record_id, db );
			break;
		case SimpleMimeType::VIDEO:
			break;
		case SimpleMimeType::ANIMATION:
			break;
		case SimpleMimeType::AUDIO:
			break;
		default:
			break;
	}

	co_return;
}

drogon::Task< drogon::HttpResponsePtr > IDHANRecordAPI::fetchInfo( drogon::HttpRequestPtr request, RecordID record_id )
{
	auto db { drogon::app().getDbClient() };

	Json::Value root {};
	root[ "record_id" ] = record_id;

	const auto record_info { co_await db->execSqlCoro( "SELECT * FROM records WHERE record_id = $1", record_id ) };

	if ( record_info.empty() ) co_return createBadRequest( "Record ID was not found" );

	root[ "hashes" ][ "sha256" ] = SHA256::fromPgCol( record_info[ 0 ][ "sha256" ] ).hex();

	const auto file_info { co_await db->execSqlCoro( "SELECT * FROM file_info WHERE record_id = $1", record_id ) };

	if ( !file_info.empty() )
	{
		root[ "size" ] = file_info[ 0 ][ "size" ].as< std::size_t >();
	}

	if ( !file_info.empty() )
	{
		const auto mime_info {
			co_await db
				->execSqlCoro( "SELECT * FROM mime WHERE mime_id = $1", file_info[ 0 ][ "mime_id" ].as< MimeID >() )
		};

		root[ "mime" ] = mime_info[ 0 ][ "name" ].as< std::string >();
		root[ "extension" ] = mime_info[ 0 ][ "best_extension" ].as< std::string >();

		co_await addFileSpecificInfo( root, file_info[ 0 ], record_id, db );
	}

	co_return drogon::HttpResponse::newHttpJsonResponse( root );
}

drogon::Task< drogon::HttpResponsePtr > IDHANRecordAPI::parseFile( drogon::HttpRequestPtr request, RecordID record_id )
{
	{
		auto db { drogon::app().getDbClient() };
		co_await tryParseRecordMetadata( record_id, db );
	}

	co_return co_await fetchInfo( request, record_id );
}

} // namespace idhan::api