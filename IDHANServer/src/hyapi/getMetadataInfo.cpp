//
// Created by kj16609 on 7/23/25.
//

#include "HyAPI.hpp"
#include "IDHANTypes.hpp"
#include "api/IDHANTagAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/records.hpp"
#include "constants/hydrus_version.hpp"
#include "core/search/SearchBuilder.hpp"
#include "crypto/SHA256.hpp"
#include "metadata/parseMetadata.hpp"

namespace idhan::hyapi
{
drogon::Task< std::expected< Json::Value, drogon::HttpResponsePtr > >
	getFileInfo( drogon::orm::DbClientPtr db, const RecordID record_id, Json::Value data )
{
	const auto file_info { co_await db->execSqlCoro(
		"SELECT size, mime.name as mime_name, coalesce(extension, best_extension) as extension FROM file_info LEFT JOIN mime ON mime.mime_id = file_info.mime_id WHERE record_id = $1",
		record_id ) };

	if ( file_info.size() == 0 )
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
	// TODO: Provide height/width information
	auto metadata {
		co_await db->execSqlCoro( "SELECT simple_mime_type FROM metadata WHERE record_id = $1", record_id )
	};

	if ( metadata.empty() )
	{
		const auto parse_result { co_await api::tryParseRecordMetadata( record_id, db ) };
		if ( !parse_result.has_value() ) co_return std::unexpected( parse_result.error() );
	}

	metadata = co_await db->execSqlCoro( "SELECT simple_mime_type FROM metadata WHERE record_id = $1", record_id );

	if ( metadata.empty() )
		co_return std::unexpected( createInternalError( "Failed to get mime type for record {}", record_id ) );

	const SimpleMimeType simple_mime_type { metadata[ 0 ][ "simple_mime_type" ].as< std::uint16_t >() };

	switch ( simple_mime_type )
	{
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
					data[ "width" ] = image_metadata[ 0 ][ 0 ].as< std::uint32_t >();
					data[ "height" ] = image_metadata[ 0 ][ 1 ].as< std::uint32_t >();
				}
				else
				{
					data[ "width" ] = 0;
					data[ "height" ] = 0;
				}
				break;
			}
		default:
			co_return std::unexpected(
				createInternalError( "Given file with unhandlable simple mime type for record {}", record_id ) );
	}

	co_return data;
}

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::fileMetadata( drogon::HttpRequestPtr request )
{
	auto db { drogon::app().getDbClient() };

	if ( auto hashes_opt = request->getOptionalParameter< std::string >( "hashes" ); hashes_opt.has_value() )
	{
		// convert hashes to their respective record_ids
		if ( auto result = co_await convertHashes( request, hashes_opt.value(), db ); !result.has_value() )
			co_return result.error();
	}

	const auto file_ids { request->getOptionalParameter< std::string >( "file_ids" ) };
	if ( !file_ids.has_value() ) co_return createBadRequest( "Must provide file_ids array" );

	std::string file_ids_str { file_ids.value() };
	Json::Value file_ids_json {};
	Json::Reader file_ids_reader {};
	file_ids_reader.parse( file_ids_str, file_ids_json );

	std::vector< RecordID > record_ids {};
	for ( const auto& id : file_ids_json ) record_ids.push_back( id.as< RecordID >() );

	// we've gotten all the ids. For now we'll just return them
	Json::Value metadata_json {};

	const auto services { co_await getServiceList( db ) };

	for ( const auto& record_id : record_ids )
	{
		// poke the metadata endpoint from /records/{record_id}/metadata in order to ensure the metadata state is valid for this record_id
		Json::Value data {};

		const auto hash_result { co_await db->execSqlCoro( "SELECT * FROM records WHERE record_id = $1", record_id ) };

		data[ "file_id" ] = record_id;
		const SHA256 sha256 { hash_result[ 0 ][ "sha256" ] };
		data[ "hash" ] = sha256.hex();

		{
			const auto data_result { co_await getFileInfo( db, record_id, data ) };
			if ( !data_result.has_value() ) co_return data_result.error();
			data = data_result.value();
		}

		data[ "file_services" ][ "current" ][ "0" ][ "time_imported" ] = 0;
		data[ "known_urls" ] = Json::Value( Json::arrayValue );

		{
			const auto data_result { co_await getMetadataInfo( db, record_id, data ) };
			if ( !data_result.has_value() ) co_return data_result.error();
			data = data_result.value();
		}

		auto storage_tags { db->execSqlCoro(
			"SELECT domain_id, tag_id, tag_text FROM tag_mappings NATURAL JOIN tags_combined WHERE record_id = $1",
			record_id ) };

		auto display_tags { db->execSqlCoro(
			"SELECT domain_id, tag_id, tag_text FROM tag_mappings_final NATURAL JOIN tags_combined WHERE record_id = $1",
			record_id ) };

		for ( const auto& storage_tag : co_await storage_tags )
		{
			const auto& domain_id { storage_tag[ "domain_id" ] };
			const auto& tag_id { storage_tag[ "tag_id" ] };
			const auto& tag_text { storage_tag[ "tag_text" ] };

			const auto service_key {
				format_ns::format( "{}-{}", hydrus::gen_constants::LOCAL_TAG, domain_id.as< TagDomainID >() )
			};

			data[ "tags" ][ service_key ][ "storage_tags" ][ "0" ].append( tag_text.as< std::string >() );
		}

		for ( const auto& display_tag : co_await display_tags )
		{
			const auto& domain_id { display_tag[ "domain_id" ] };
			const auto& tag_id { display_tag[ "tag_id" ] };
			const auto& tag_text { display_tag[ "tag_text" ] };

			const auto service_key {
				format_ns::format( "{}-{}", hydrus::gen_constants::LOCAL_TAG, domain_id.as< TagDomainID >() )
			};

			data[ "tags" ][ service_key ][ "display_tags" ][ "0" ].append( tag_text.as< std::string >() );
		}

		for ( const auto& service : services )
		{
			const auto service_key { service[ "service_key" ].asString() };

			if ( !data[ "tags" ].isMember( service_key ) ) continue;

			data[ "tags" ][ service_key ][ "name" ] = service[ "name" ];
			data[ "tags" ][ service_key ][ "type_pretty" ] = service[ "type_pretty" ];
			data[ "tags" ][ service_key ][ "type" ] = service[ "type" ];
		}

		metadata_json.append( std::move( data ) );
	}

	Json::Value out {};
	out[ "metadata" ] = std::move( metadata_json );

	out[ "services" ] = std::move( services );

	co_return drogon::HttpResponse::newHttpJsonResponse( std::move( out ) );
}
} // namespace idhan::hyapi