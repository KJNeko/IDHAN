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
	tryParseRecordMetadata( const RecordID record_id, drogon::orm::DbClientPtr db )
{
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

	auto& parser { parsers.front() };

	const auto record_path { co_await helpers::getRecordPath( record_id, db ) };

	if ( !record_path.has_value() ) co_return std::unexpected( record_path.error() );

	if ( !std::filesystem::exists( record_path.value() ) )
		co_return std::unexpected( createInternalError( "File for record {} was missing", record_id ) );

	FileMappedData data { record_path.value() };

	const auto metadata {
		parser->parseImage( data.data(), data.length(), mime_info[ 0 ][ "mime_name" ].as< std::string >() )
	};

	if ( !metadata.has_value() )
	{
		const ModuleError error { metadata.error() };
		auto ret { createInternalError( "Module failed to parse data: {}", error ) };
		co_return std::unexpected( ret );
	}

	const auto simple_type { metadata.value().m_simple_type };

	Json::Value json {};
	Json::Reader reader {};
	if ( !metadata.value().m_extra.empty() )
	{
		if ( !reader.parse( metadata.value().m_extra, json ) )
			co_return std::
				unexpected( createBadRequest( "Failed to parse metadata \"{}\"", metadata.value().m_extra ) );
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
				const auto& image_metadata { std::get< MetadataInfoImage >( metadata.value().m_metadata ) };
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



} // namespace idhan::api
