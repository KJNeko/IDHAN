//
// Created by kj16609 on 6/12/25.
//

#include "parseMetadata.hpp"

#include <drogon/drogon.h>
#include <json/json.h>

#include "api/helpers/ExpectedTask.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"
#include "filesystem/IOUring.hpp"
#include "modules/ModuleLoader.hpp"

namespace idhan::api
{

ExpectedTask< void > updateRecordMetadata( const RecordID record_id, DbClientPtr db, MetadataInfo metadata )
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

		co_await db->execSqlCoro(
			"UPDATE metadata SET json = $2 WHERE record_id = $1", record_id, json.toStyledString() );
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
			FGL_UNIMPLEMENTED();
			break;
		case SimpleMimeType::ANIMATION:
			FGL_UNIMPLEMENTED();
			break;
		case SimpleMimeType::AUDIO:
			FGL_UNIMPLEMENTED();
			break;
		case SimpleMimeType::NONE:
			break;
		default:;
	}

	co_return {};
}

drogon::Task< MetadataInfo > getMetadata( [[maybe_unused]] const RecordID record_id, [[maybe_unused]] DbClientPtr db )
{
	FGL_UNIMPLEMENTED();
}

drogon::Task< std::shared_ptr< MetadataModuleI > > findBestParser( const std::string mime_name )
{
	auto parsers { modules::ModuleLoader::instance().getParserFor( mime_name ) };

	if ( parsers.empty() ) co_return {};

	// return the first parser
	co_return parsers[ 0 ];
}

ExpectedTask< FileIOUring > getIOForRecord( const RecordID record_id, DbClientPtr db )
{
	const auto path { co_await helpers::getRecordPath( record_id, db ) };
	return_unexpected_error( path );

	if ( !std::filesystem::exists( *path ) )
	{
		co_return std::unexpected(
			createInternalError( "Record {} does not exist at the expected path {}.", record_id, path->string() ) );
	}

	FileIOUring uring { *path };
	co_return uring;
}

ExpectedTask< void > tryParseRecordMetadata( const RecordID record_id, DbClientPtr db )
{
	const auto metadata { co_await parseMetadata( record_id, db ) };
	return_unexpected_error( metadata );

	co_await updateRecordMetadata( record_id, db, metadata.value() );

	co_return {};
}

ExpectedTask< MetadataInfo > parseMetadata( const RecordID record_id, DbClientPtr db )
{
	auto io { co_await getIOForRecord( record_id, db ) };
	if ( !io ) co_return std::unexpected( createBadRequest( "Record {} does not exist", record_id ) );
	const auto [ data, length ] = io->mmap();

	const auto record_mime {
		co_await db->execSqlCoro( "SELECT mime_id FROM file_info WHERE record_id = $1", record_id )
	};

	if ( record_mime.empty() )
		co_return std::unexpected( createBadRequest(
			"Record {} does not exist or does not have any file info associated with it", record_id ) );

	if ( record_mime[ 0 ][ "mime_id" ].isNull() ) co_return MetadataInfo {};

	const auto mime_id { record_mime[ 0 ][ "mime_id" ].as< MimeID >() };

	const auto mime_info { co_await db->execSqlCoro( "SELECT * FROM mime WHERE mime_id = $1", mime_id ) };

	if ( mime_info.empty() )
		co_return std::unexpected( createInternalError( "Unable to get mime info for mime_id {}", mime_id ) );

	const auto mime_name { mime_info[ 0 ][ "name" ].as< std::string >() };

	const std::shared_ptr< MetadataModuleI > parser { co_await findBestParser( mime_name ) };

	if ( parser == nullptr )
		co_return std::unexpected( createBadRequest( "No parser found for mime type {}", mime_name ) );

	const auto metadata { parser->parseFile( data, length, mime_name ) };

	if ( !metadata )
	{
		const ModuleError error { metadata.error() };
		auto ret { createInternalError( "Module failed to parse data for record {}: {}", record_id, error ) };
		co_return std::unexpected( ret );
	}

	co_return metadata.value();
}

} // namespace idhan::api
