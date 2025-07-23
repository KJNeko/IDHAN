//
// Created by kj16609 on 6/12/25.
//

#include "parseMetadata.hpp"

#include <drogon/drogon.h>
#include <json/json.h>

#include "FileMappedData.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"
#include "modules/ModuleLoader.hpp"

namespace idhan::api
{

drogon::Task< std::expected< void, drogon::HttpResponsePtr > >
	updateRecordMetadata( const RecordID record_id, drogon::orm::DbClientPtr db, MetadataInfo metadata )
{
	const auto simple_type { metadata.m_simple_type };

	co_await db->execSqlCoro(
		"INSERT INTO metadata (record_id, simple_mime_type) VALUES ($1, $2) "
		"ON CONFLICT (record_id) DO UPDATE SET simple_mime_type = $2",
		record_id,
		simple_type );

	Json::Value json {};
	Json::Reader reader {};
	if ( !metadata.m_extra.empty() )
	{
		if ( !reader.parse( metadata.m_extra, json ) )
			co_return std::unexpected( createBadRequest( "Failed to parse metadata \"{}\"", metadata.m_extra ) );

		co_await db
			->execSqlCoro( "UPDATE metadata SET json = $2 WHERE record_id = $1", record_id, json.toStyledString() );
	}

	switch ( simple_type )
	{
		case SimpleMimeType::IMAGE:
			{
				const auto& image_metadata { std::get< MetadataInfoImage >( metadata.m_metadata ) };
				co_await db->execSqlCoro(
					"INSERT INTO image_metadata (record_id, width, height, channels) VALUES ($1, $2, $3, $4) "
					"ON CONFLICT (record_id) DO UPDATE SET width = $2, height = $3, channels = $4",
					record_id,
					image_metadata.width,
					image_metadata.height,
					static_cast< std::uint16_t >( image_metadata.channels ) );
				break;
			}
		case SimpleMimeType::VIDEO:
			break;
		case SimpleMimeType::ANIMATION:
			break;
		case SimpleMimeType::AUDIO:
			break;
		default:;
	}

	co_return {};
}

drogon::Task< std::shared_ptr< MetadataModuleI > > findBestParser( const std::string mime_name )
{
	auto parsers { modules::ModuleLoader::instance().getParserFor( mime_name ) };

	if ( parsers.empty() ) co_return {};

	// return the first parser
	co_return parsers[ 0 ];
}

drogon::Task< std::expected< MetadataInfo, drogon::HttpResponsePtr > >
	getMetadata( const RecordID record_id, const std::shared_ptr< FileMappedData > data, drogon::orm::DbClientPtr db )
{
	const auto record_mime {
		co_await db->execSqlCoro( "SELECT mime_id FROM file_info WHERE record_id = $1", record_id )
	};

	if ( record_mime.empty() )
		co_return std::unexpected( createBadRequest(
			"Record {} does not exist or does not have any file info associated with it", record_id ) );

	const auto mime_id { record_mime[ 0 ][ "mime_id" ].as< MimeID >() };

	const auto mime_info { co_await db->execSqlCoro( "SELECT * FROM mime WHERE mime_id = $1", mime_id ) };

	if ( mime_info.empty() )
		co_return std::unexpected( createInternalError( "Unable to get mime info for mime_id {}", mime_id ) );

	const auto mime_name { mime_info[ 0 ][ "name" ].as< std::string >() };

	const std::shared_ptr< MetadataModuleI > parser { co_await findBestParser( mime_name ) };

	if ( parser == nullptr )
		co_return std::unexpected( createBadRequest( "No parser found for mime type {}", mime_name ) );

	const auto metadata { parser->parseFile( data->data(), data->length(), mime_name ) };

	if ( !metadata.has_value() )
	{
		const ModuleError error { metadata.error() };
		auto ret { createInternalError( "Module failed to parse data: {}", error ) };
		co_return std::unexpected( ret );
	}

	co_return metadata.value();
}

drogon::Task< std::expected< void, drogon::HttpResponsePtr > >
	tryParseRecordMetadata( const RecordID record_id, drogon::orm::DbClientPtr db )
{
	// Get the file info
	const auto record_path { co_await helpers::getRecordPath( record_id, db ) };

	if ( !record_path.has_value() ) co_return std::unexpected( record_path.error() );

	if ( !std::filesystem::exists( record_path.value() ) )
		co_return std::unexpected( createInternalError( "File for record {} was missing", record_id ) );

	const auto data { std::make_shared< FileMappedData >( record_path.value() ) };

	const auto metadata { co_await getMetadata( record_id, data, db ) };

	if ( !metadata.has_value() ) co_return std::unexpected( metadata.error() );

	co_await updateRecordMetadata( record_id, db, metadata.value() );

	co_return {};
}

} // namespace idhan::api
