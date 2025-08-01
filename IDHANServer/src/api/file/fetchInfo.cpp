//
// Created by kj16609 on 6/12/25.
//

#include "IDHANTypes.hpp"
#include "api/RecordAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "crypto/SHA256.hpp"
#include "metadata/parseMetadata.hpp"

namespace idhan::api
{

drogon::Task< std::expected< void, drogon::HttpResponsePtr > >
	addImageInfo( Json::Value& root, const RecordID record_id, drogon::orm::DbClientPtr db )
{
	const auto metadata { co_await db->execSqlCoro( "SELECT * FROM image_metadata WHERE record_id = $1", record_id ) };

	if ( metadata.empty() )
		co_return std::unexpected( createInternalError( "Could not find image metadata for record {}", record_id ) );

	root[ "width" ] = metadata[ 0 ][ "width" ].as< std::uint32_t >();
	root[ "height" ] = metadata[ 0 ][ "height" ].as< std::uint32_t >();
	root[ "channels" ] = metadata[ 0 ][ "channels" ].as< std::uint32_t >();

	co_return {};
}

drogon::Task< std::expected< void, drogon::HttpResponsePtr > >
	addFileSpecificInfo( Json::Value& root, const RecordID record_id, drogon::orm::DbClientPtr db )
{
	auto simple_mime_result {
		co_await db->execSqlCoro( "SELECT simple_mime_type FROM metadata WHERE record_id = $1", record_id )
	};

	if ( simple_mime_result.empty() ) // Could not find any mime info for this record, Try parsing for it.
	{
		const auto parsed_metadata { co_await tryParseRecordMetadata( record_id, db ) };

		if ( !parsed_metadata ) co_return std::unexpected( parsed_metadata.error() );

		simple_mime_result =
			co_await db->execSqlCoro( "SELECT simple_mime_type FROM metadata WHERE record_id = $1", record_id );
	}

	if ( simple_mime_result.empty() )
		co_return std::unexpected( createInternalError( "Failed to get simple mime type for record {}", record_id ) );

	const SimpleMimeType simple_mime_type { simple_mime_result[ 0 ][ "simple_mime_type" ].as< std::uint16_t >() };

	switch ( simple_mime_type )
	{
		case SimpleMimeType::IMAGE:
			{
				const auto result { co_await addImageInfo( root, record_id, db ) };
				if ( !result ) co_return std::unexpected( result.error() );
				break;
			}
		case SimpleMimeType::VIDEO:
			break;
		case SimpleMimeType::ANIMATION:
			break;
		case SimpleMimeType::AUDIO:
			break;
		default:
			break;
	}

	co_return {};
}

drogon::Task< drogon::HttpResponsePtr > RecordAPI::
	fetchInfo( [[maybe_unused]] drogon::HttpRequestPtr request, RecordID record_id )
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

		const auto mime_info {
			co_await db
				->execSqlCoro( "SELECT * FROM mime WHERE mime_id = $1", file_info[ 0 ][ "mime_id" ].as< MimeID >() )
		};

		root[ "mime" ] = mime_info[ 0 ][ "name" ].as< std::string >();
		root[ "extension" ] = mime_info[ 0 ][ "best_extension" ].as< std::string >();

		co_await addFileSpecificInfo( root, record_id, db );
	}

	co_return drogon::HttpResponse::newHttpJsonResponse( root );
}

drogon::Task< drogon::HttpResponsePtr > RecordAPI::parseFile( drogon::HttpRequestPtr request, RecordID record_id )
{
	{
		auto db { drogon::app().getDbClient() };
		const auto parse_result { co_await tryParseRecordMetadata( record_id, db ) };
		if ( !parse_result ) co_return parse_result.error();
	}

	co_return co_await fetchInfo( request, record_id );
}

} // namespace idhan::api