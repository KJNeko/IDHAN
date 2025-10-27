//
// Created by kj16609 on 7/23/25.
//

#include "HyAPI.hpp"
#include "IDHANTypes.hpp"
#include "api/TagAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/records.hpp"
#include "api/record/urls/urls.hpp"
#include "constants/hydrus_version.hpp"
#include "core/search/SearchBuilder.hpp"
#include "crypto/SHA256.hpp"
#include "db/drogonArrayBind.hpp"
#include "drogon/utils/coroutine.h"
#include "fgl/defines.hpp"
#include "logging/ScopedTimer.hpp"
#include "metadata/parseMetadata.hpp"

namespace idhan::hyapi
{
drogon::Task< std::expected< Json::Value, drogon::HttpResponsePtr > >
	getFileInfo( drogon::orm::DbClientPtr db, const RecordID record_id, Json::Value data )
{
	const auto file_info { co_await db->execSqlCoro(
		"SELECT size, mime.name as mime_name, coalesce(extension, best_extension) as extension FROM file_info LEFT JOIN mime ON mime.mime_id = file_info.mime_id WHERE record_id = $1",
		record_id ) };

	if ( file_info.empty() )
	{
		data[ "size" ] = 0;
		data[ "mime" ] = "";
		data[ "ext" ] = "";
	}
	else
	{
		data[ "size" ] = file_info[ 0 ][ "size" ].as< std::size_t >();
		data[ "mime" ] = file_info[ 0 ][ "mime_name" ].as< std::string >();
		data[ "ext" ] = file_info[ 0 ][ "extension" ].as< std::string >();
	}

	co_return data;
}

drogon::Task< std::expected< Json::Value, drogon::HttpResponsePtr > >
	getMetadataInfo( drogon::orm::DbClientPtr db, const RecordID record_id, Json::Value data )
{
	auto metadata = co_await db->execSqlCoro( "SELECT simple_mime_type FROM metadata WHERE record_id = $1", record_id );

	if ( metadata.empty() )
	{
		log::warn( "Metadata missing for record {} Attempting to acquire metadata", record_id );
		const auto parse_result { co_await api::tryParseRecordMetadata( record_id, db ) };
		if ( !parse_result ) co_return std::unexpected( parse_result.error() );

		metadata = co_await db->execSqlCoro( "SELECT simple_mime_type FROM metadata WHERE record_id = $1", record_id );

		if ( metadata.empty() )
			co_return std::unexpected( createInternalError( "Failed to get mime type for record {}", record_id ) );
	}

	const SimpleMimeType simple_mime_type { metadata[ 0 ][ "simple_mime_type" ].as< std::uint16_t >() };

	data[ "filetype_enum" ] =
		static_cast< Json::Value::Int >( hydrus::hy_constants::simpleToHyType( simple_mime_type ) );

	switch ( simple_mime_type )
	{
		case SimpleMimeType::VIDEO:
			FGL_UNIMPLEMENTED();
		case SimpleMimeType::NONE:
			//NOOP
			break;
		case SimpleMimeType::IMAGE:
			{
				const auto image_metadata {
					co_await db
						->execSqlCoro( "SELECT width, height FROM image_metadata WHERE record_id = $1", record_id )
				};

				if ( !image_metadata.empty() )
				{
					data[ "width" ] = image_metadata[ 0 ][ "width" ].as< std::uint32_t >();
					data[ "height" ] = image_metadata[ 0 ][ "height" ].as< std::uint32_t >();
				}
				else
				{
					data[ "width" ] = 0;
					data[ "height" ] = 0;
				}
				break;
			}
		case SimpleMimeType::ANIMATION:
			FGL_UNIMPLEMENTED();
			break;
		case SimpleMimeType::AUDIO:
			FGL_UNIMPLEMENTED();
			break;
		case SimpleMimeType::ARCHIVE:
			FGL_UNIMPLEMENTED();
			break;
		case SimpleMimeType::IMAGE_PROJECT:
			FGL_UNIMPLEMENTED();
			break;
		default:
			co_return std::unexpected(
				createInternalError( "Given file with unhandlable simple mime type for record {}", record_id ) );
	}

	co_return data;
}

drogon::Task< std::expected< Json::Value, drogon::HttpResponsePtr > >
	getMetadataFromRow( drogon::orm::DbClientPtr db, const Json::Value services, const drogon::orm::Row row )
{
	const auto& record_id { row[ 0 ].as< RecordID >() };
	const auto sha256 { SHA256::fromPgCol( row[ 1 ] ) };
	const auto& size { row[ 2 ].as< std::size_t >() };
	const auto mime_name { row[ 3 ].as< std::string_view >() };
	const auto extension { row[ 4 ].as< std::string_view >() };
	const auto cluster_store_time_timestamp { row[ "cluster_store_time" ].isNull() ?
		                                          0 :
		                                          row[ "cluster_store_time" ].as< std::size_t >() };

	Json::Value data {};

	data[ "file_id" ] = record_id;
	data[ "hash" ] = sha256.hex();

	data[ "size" ] = size;
	data[ "mime" ] = std::string( mime_name );
	data[ "ext" ] = std::string( extension );

	data[ "file_services" ][ "current" ][ "0" ][ "time_imported" ] = cluster_store_time_timestamp;

	const auto url_json_e { co_await fetchUrlsStrings( record_id, db ) };
	if ( !url_json_e ) co_return std::unexpected( url_json_e.error() );

	data[ "known_urls" ] = Json::Value( Json::arrayValue );
	data[ "detailed_known_urls" ] = Json::Value( Json::arrayValue );

	for ( const auto& url_str : url_json_e.value() )
	{
		data[ "known_urls" ].append( url_str );

		Json::Value advanced_url_info {};
		advanced_url_info[ "request_url" ] = url_str;
		advanced_url_info[ "normalised_url" ] = url_str;
		advanced_url_info[ "url_type" ] = 5; // Unknown URL
		advanced_url_info[ "url_type_string" ] = "unknown";
		advanced_url_info[ "can_parse" ] = false;

		data[ "detailed_known_urls" ].append( advanced_url_info );
	}

	{
		logging::ScopedTimer metadata_timer { "metadata", std::chrono::milliseconds( 5 ) };
		const auto data_result { co_await getMetadataInfo( db, record_id, data ) };
		if ( data_result ) data = data_result.value();
	}

	auto storage_tags { db->execSqlCoro(
		"SELECT tag_domain_id, tag_id, tag_text FROM active_tag_mappings NATURAL JOIN tags_combined WHERE record_id = $1",
		record_id ) };

	data[ "tags" ] = Json::Value( Json::objectValue );

	for ( const auto& storage_tag : co_await storage_tags )
	{
		const auto& tag_domain_id { storage_tag[ "tag_domain_id" ] };
		const auto& tag_id { storage_tag[ "tag_id" ] };
		const auto& tag_text { storage_tag[ "tag_text" ] };

		const auto service_key { format_ns::format(
			"{}-{}", hydrus::gen_constants::LOCAL_TAG, tag_domain_id.drogon::orm::Field::as< TagDomainID >() ) };

		data[ "tags" ][ service_key ][ "storage_tags" ][ "0" ].Json::Value::append( tag_text.as< std::string >() );
	}

	auto display_tags { db->execSqlCoro(
		"SELECT tag_domain_id, tag_id, tag_text FROM active_tag_mappings NATURAL JOIN tags_combined WHERE record_id = $1"
		" UNION DISTINCT "
		"SELECT tag_domain_id, tag_id, tag_text FROM active_tag_mappings_parents NATURAL JOIN tags_combined WHERE record_id = $1",
		record_id ) };
	for ( const auto& display_tag : co_await display_tags )
	{
		const auto& tag_domain_id { display_tag[ "tag_domain_id" ] };
		const auto& tag_id { display_tag[ "tag_id" ] };
		const auto& tag_text { display_tag[ "tag_text" ] };

		const auto service_key { format_ns::format(
			"{}-{}", hydrus::gen_constants::LOCAL_TAG, tag_domain_id.drogon::orm::Field::as< TagDomainID >() ) };

		data[ "tags" ][ service_key ][ "display_tags" ][ "0" ].Json::Value::append( tag_text.as< std::string >() );
	}

	for ( const auto& service : services )
	{
		const auto service_key { service[ "service_key" ].asString() };

		if ( !data[ "tags" ].isMember( service_key ) ) continue;

		data[ "tags" ][ service_key ][ "name" ] = service[ "name" ];
		data[ "tags" ][ service_key ][ "type_pretty" ] = service[ "type_pretty" ];
		data[ "tags" ][ service_key ][ "type" ] = service[ "type" ];
	}

	co_return data;
}

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::fileMetadata( drogon::HttpRequestPtr request )
{
	logging::ScopedTimer timer { "fileMetadata", std::chrono::milliseconds( 250 ) };
	auto db { drogon::app().getDbClient() };

	if ( auto hashes_opt = request->getOptionalParameter< std::string >( "hashes" ) )
	{
		// convert hashes to their respective record_ids
		if ( auto result = co_await convertQueryRecordIDs( request, db ); !result ) co_return result.error();
	}

	const auto file_ids { request->getOptionalParameter< std::string >( "file_ids" ) };
	if ( !file_ids ) co_return createBadRequest( "Must provide file_ids array" );

	const std::string& file_ids_str { file_ids.value() };
	Json::Value file_ids_json {};
	Json::Reader file_ids_reader {};
	file_ids_reader.parse( file_ids_str, file_ids_json );

	std::vector< RecordID > record_ids {};
	record_ids.reserve( file_ids_json.size() );
	for ( const auto& id : file_ids_json ) record_ids.push_back( id.as< RecordID >() );

	Json::Value metadata_json {};

	const auto services { co_await getServiceList( db ) };

	const auto hash_result { co_await db->execSqlCoro(
		"SELECT record_id, sha256, coalesce(size, 0), coalesce(mime.name, '') as mime_name, coalesce(coalesce(extension, best_extension), '') as extension, cluster_store_time FROM records LEFT JOIN file_info USING (record_id) LEFT JOIN mime USING (mime_id) WHERE record_id = ANY($1::" RECORD_PG_TYPE_NAME
		"[])",
		std::move( record_ids ) ) };

	struct Info
	{
		RecordID record_id;
		SHA256 sha256;
		std::size_t size;
		std::string mime_name;
		std::string extension;
	};

	// auto response { drogon::HttpResponse::newAsyncStreamResponse() };

	std::vector< drogon::Task< std::expected< Json::Value, drogon::HttpResponsePtr > > > tasks {};

	for ( const auto& row : hash_result )
	{
		tasks.emplace_back( getMetadataFromRow( db, services, row ) );
	}

	Json::Value out {};
	out[ "metadata" ] = std::move( metadata_json );

	auto when_all_awaiter { drogon::when_all( std::move( tasks ) ) };
	auto values { co_await when_all_awaiter };

	for ( const auto& json_value : values )
	{
		if ( !json_value ) co_return json_value.error();

		out[ "metadata" ].append( *json_value );
	}

	out[ "services" ] = services;

	co_return drogon::HttpResponse::newHttpJsonResponse( std::move( out ) );
}
} // namespace idhan::hyapi