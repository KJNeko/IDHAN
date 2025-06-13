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

	Json::Value json {};
	Json::Reader reader {};
	if ( !metadata.m_extra.empty() )
	{
		if ( !reader.parse( metadata.m_extra, json ) )
			co_return std::unexpected( createBadRequest( "Failed to parse metadata \"{}\"", metadata.m_extra ) );
	}
	else
	{
		json = Json::ValueType::objectValue;
	}

	co_await db->execSqlCoro(
		"INSERT INTO metadata (record_id, simple_mime_type, json) VALUES ($1, $2, $3::json) "
		"ON CONFLICT (record_id) DO UPDATE SET simple_mime_type = $2, json = $3::json",
		record_id,
		simple_type,
		json );

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

drogon::Task< std::expected< std::pair< std::shared_ptr< MetadataModuleI >, std::string >, drogon::HttpResponsePtr > >
	findBestParser( const RecordID record_id, drogon::orm::DbClientPtr db )
{
	// Get the mime info needed to determine which parser to use
	const auto mime_info { co_await db->execSqlCoro(
		"SELECT mime.name AS mime_name FROM file_info JOIN mime ON mime.mime_id = file_info.mime_id WHERE record_id = $1",
		record_id ) };

	if ( mime_info.empty() )
		co_return std::unexpected( createInternalError( "No mime info for record {}", record_id ) );

	auto parsers {
		modules::ModuleLoader::instance().getParserFor( mime_info[ 0 ][ "mime_name" ].as< std::string >() )
	};

	if ( parsers.empty() )
	{
		co_return std::unexpected(
			createInternalError( "No parser for mime {}", mime_info[ 0 ][ "mime_name" ].as< std::string >() ) );
	}

	co_return std::make_pair( parsers.front(), mime_info[ 0 ][ "mime_name" ].as< std::string >() );
}

drogon::Task< std::expected< MetadataInfo, drogon::HttpResponsePtr > >
	getMetadata( const RecordID record_id, const std::shared_ptr< FileMappedData > data, drogon::orm::DbClientPtr db )
{
	auto parser_e { co_await findBestParser( record_id, db ) };
	if ( !parser_e.has_value() ) co_return std::unexpected( parser_e.error() );

	auto& [ parser, mime_name ] = parser_e.value();

	const auto metadata { parser->parseImage( data->data(), data->length(), mime_name ) };

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

	std::shared_ptr< FileMappedData > data { std::make_shared< FileMappedData >( record_path.value() ) };

	const auto metadata { co_await getMetadata( record_id, data, db ) };

	if ( !metadata.has_value() ) co_return std::unexpected( metadata.error() );

	co_await updateRecordMetadata( record_id, db, metadata.value() );

	co_return {};
}

} // namespace idhan::api
