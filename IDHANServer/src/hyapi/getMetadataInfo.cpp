#include <algorithm>
#include <array>
#include <ranges>

#include "HyAPI.hpp"
#include "IDHANTypes.hpp"
#include "api/TagAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/record/urls/urls.hpp"
#include "constants/hydrus_version.hpp"
#include "core/search/SearchBuilder.hpp"
#include "crypto/SHA256.hpp"
#include "db/drogonArrayBind.hpp"
#include "drogon/utils/coroutine.h"
#include "fgl/defines.hpp"
#include "helpers.hpp"
#include "hyapi/services/Services.hpp"
#include "logging/ScopedTimer.hpp"
#include "metadata/metadata.hpp"
#include "records/records.hpp"

namespace idhan::hyapi
{
//! The keys every record's metadata object is built from.
//!
//! Inserting through a Json::StaticString stores the pointer instead of copying the key, which is one allocation
//! saved per key per record. Only inserts are worth spelling this way; a read already looks up without copying.
//! Every one of these must outlive the values built with it, which a literal does.
static const Json::StaticString KEY_FILE_ID { "file_id" };
static const Json::StaticString KEY_HASH { "hash" };
static const Json::StaticString KEY_SIZE { "size" };
static const Json::StaticString KEY_MIME { "mime" };
static const Json::StaticString KEY_EXT { "ext" };
static const Json::StaticString KEY_FILE_SERVICES { "file_services" };
static const Json::StaticString KEY_CURRENT { "current" };
static const Json::StaticString KEY_DELETED { "deleted" };
static const Json::StaticString KEY_TIME_IMPORTED { "time_imported" };
static const Json::StaticString KEY_KNOWN_URLS { "known_urls" };
static const Json::StaticString KEY_DETAILED_KNOWN_URLS { "detailed_known_urls" };
static const Json::StaticString KEY_REQUEST_URL { "request_url" };
static const Json::StaticString KEY_NORMALISED_URL { "normalised_url" };
static const Json::StaticString KEY_URL_TYPE { "url_type" };
static const Json::StaticString KEY_URL_TYPE_STRING { "url_type_string" };
static const Json::StaticString KEY_CAN_PARSE { "can_parse" };
static const Json::StaticString KEY_TAGS { "tags" };
static const Json::StaticString KEY_STORAGE_TAGS { "storage_tags" };
static const Json::StaticString KEY_DISPLAY_TAGS { "display_tags" };
static const Json::StaticString KEY_NAME { "name" };
static const Json::StaticString KEY_TYPE_PRETTY { "type_pretty" };
static const Json::StaticString KEY_TYPE { "type" };
static const Json::StaticString KEY_FILETYPE_ENUM { "filetype_enum" };
static const Json::StaticString KEY_METADATA { "metadata" };
static const Json::StaticString KEY_SERVICES { "services" };
static const Json::StaticString KEY_SERVICES_V2 { "services_v2" };

drogon::Task< std::expected< Json::Value, drogon::HttpResponsePtr > > getFileInfo(
	DbClientPtr db,
	const RecordID record_id,
	Json::Value data )
{
	const auto file_info { co_await db->execSqlCoro(
		"SELECT size, mime.name as mime_name, coalesce(extension, best_extension) as extension FROM file_info LEFT JOIN "
		"mime ON mime.mime_id = file_info.mime_id WHERE record_id = $1",
		record_id ) };

	if ( file_info.empty() )
	{
		data[ KEY_SIZE ] = 0;
		data[ KEY_MIME ] = "";
		data[ KEY_EXT ] = "";
	}
	else
	{
		data[ KEY_SIZE ] = file_info[ 0 ][ "size" ].as< std::size_t >();
		data[ KEY_MIME ] = file_info[ 0 ][ "mime_name" ].as< std::string >();
		data[ KEY_EXT ] = helpers::withLeadingDot( file_info[ 0 ][ "extension" ].as< std::string >() );
	}

	co_return data;
}

drogon::Task< std::expected< Json::Value, drogon::HttpResponsePtr > > getMetadataInfo(
	DbClientPtr db,
	const RecordID record_id,
	Json::Value data )
{
	auto metadata = co_await db->execSqlCoro(
		"SELECT simple_mime_type, mime.name as mime_name FROM metadata JOIN file_info USING (record_id) JOIN MIME USING (mime_id) WHERE record_id = $1",
		record_id );

	if ( metadata.empty() )
	{
		log::warn( "Metadata missing for record {} Attempting to acquire metadata", record_id );
		const auto parse_result { co_await metadata::tryParseRecordMetadata( record_id, db ) };
		if ( !parse_result ) co_return std::unexpected( parse_result.error() );

		metadata = co_await db->execSqlCoro(
			"SELECT simple_mime_type, mime.name as mime_name FROM metadata JOIN file_info USING (record_id) JOIN MIME USING (mime_id) WHERE record_id = $1",
			record_id );

		if ( metadata.empty() )
			co_return std::unexpected( createInternalError( "Failed to get mime type for record {}", record_id ) );
	}

	const auto mime_name { metadata[ 0 ][ "mime_name" ].as< std::string >() };
	data[ KEY_FILETYPE_ENUM ] = hydrus::hy_constants::mimeToHyType( mime_name );

	// The collector's base fields duplicate what this response already spells in Hydrus' own naming,
	// so only its file specific keys are merged in.
	static const std::array< std::string_view, 6 > base_keys {
		{ "record_id", "hashes", "size", "mime", "extension", "parsed" }
	};

	const auto batch { co_await metadata::collectRecordInfo( { record_id }, db ) };

	if ( !batch.records.empty() )
	{
		const Json::Value& info { batch.records[ 0 ] };

		for ( const auto& key : info.getMemberNames() )
		{
			if ( std::ranges::find( base_keys, key ) != base_keys.end() ) continue;

			data[ key ] = info[ key ];
		}
	}

	co_return data;
}

//! Records whose metadata is gathered concurrently within one file_metadata request.
//!
//! Every record in flight holds its own JSON tree and the results of the four queries behind it, so this is what
//! bounds the memory one request costs. A client asking for tens of thousands of ids pays for this many at a time
//! instead of all of them at once.
static constexpr std::size_t METADATA_BATCH_SIZE { 1 };

static void appendTagsByDomain( Json::Value& tags, const Json::StaticString which, const drogon::orm::Result& rows )
{
	static const Json::StaticString FIRST_INDEX { "0" };

	TagDomainID current_domain {};
	Json::Value* target { nullptr };

	for ( const auto& row : rows )
	{
		const auto tag_domain_id { row[ "tag_domain_id" ].as< TagDomainID >() };

		if ( target == nullptr || tag_domain_id != current_domain )
		{
			current_domain = tag_domain_id;
			target = &tags[ cachedTagDomainServiceKey( tag_domain_id ) ][ which ][ FIRST_INDEX ];
		}

		const auto& tag_text { row[ "tag_text" ] };
		const auto* const begin { tag_text.c_str() };

		target->append( Json::Value( begin, begin + tag_text.length() ) );
	}
}

drogon::Task< std::expected< Json::Value, drogon::HttpResponsePtr > > getMetadataFromRow(
	DbClientPtr db,
	const Json::Value services,
	const drogon::orm::Row row )
{
	const auto& record_id { row[ 0 ].as< RecordID >() };
	const auto sha256 { SHA256::fromPgCol( row[ 1 ] ) };
	const auto& size { row[ 2 ].as< std::size_t >() };
	const auto mime_name { row[ 3 ].as< std::string_view >() };
	const auto extension { row[ 4 ].as< std::string_view >() };
	const auto cluster_store_time_timestamp {
		row[ "cluster_store_time" ].isNull() ? 0 : row[ "cluster_store_time" ].as< std::size_t >()
	};

	Json::Value data {};

	data[ KEY_FILE_ID ] = record_id;
	data[ KEY_HASH ] = sha256.hex();

	data[ KEY_SIZE ] = size;
	data[ KEY_MIME ] = std::string( mime_name );
	data[ KEY_EXT ] = helpers::withLeadingDot( extension );

	data[ KEY_FILE_SERVICES ][ KEY_CURRENT ] = Json::Value( Json::objectValue );
	data[ KEY_FILE_SERVICES ][ KEY_DELETED ] = Json::Value( Json::objectValue );

	if ( !row[ "cluster_id" ].isNull() )
	{
		const auto cluster_key { fileClusterServiceKey( row[ "cluster_id" ].as< ClusterID >() ) };

		data[ KEY_FILE_SERVICES ][ KEY_CURRENT ][ cluster_key ][ KEY_TIME_IMPORTED ] = cluster_store_time_timestamp;
	}

	const auto url_json_e { co_await fetchUrlsStrings( record_id, db ) };
	if ( !url_json_e ) co_return std::unexpected( url_json_e.error() );

	data[ KEY_KNOWN_URLS ] = Json::Value( Json::arrayValue );
	data[ KEY_DETAILED_KNOWN_URLS ] = Json::Value( Json::arrayValue );

	Json::Value& known_urls { data[ KEY_KNOWN_URLS ] };
	Json::Value& detailed_known_urls { data[ KEY_DETAILED_KNOWN_URLS ] };

	for ( const auto& url_str : url_json_e.value() )
	{
		known_urls.append( url_str );

		Json::Value advanced_url_info {};
		advanced_url_info[ KEY_REQUEST_URL ] = url_str;
		advanced_url_info[ KEY_NORMALISED_URL ] = url_str;
		advanced_url_info[ KEY_URL_TYPE ] = 5; // Unknown URL
		advanced_url_info[ KEY_URL_TYPE_STRING ] = "unknown";
		advanced_url_info[ KEY_CAN_PARSE ] = false;

		detailed_known_urls.append( std::move( advanced_url_info ) );
	}

	{
		logging::ScopedTimer metadata_timer { "metadata", std::chrono::milliseconds( 5 ) };
		const auto data_result { co_await getMetadataInfo( db, record_id, data ) };
		if ( data_result ) data = data_result.value();
	}

	auto storage_tags { db->execSqlCoro(
		"SELECT tag_domain_id, tag_id, tag_text FROM active_tag_mappings NATURAL JOIN tags WHERE record_id = $1",
		record_id ) };

	data[ KEY_TAGS ] = Json::Value( Json::objectValue );
	Json::Value& tags { data[ KEY_TAGS ] };

	const auto storage_rows { co_await storage_tags };
	appendTagsByDomain( tags, KEY_STORAGE_TAGS, storage_rows );

	auto display_tags { db->execSqlCoro(
		"SELECT tag_domain_id, tag_id, tag_text FROM active_tag_mappings NATURAL JOIN tags WHERE record_id = $1"
		" UNION DISTINCT "
		"SELECT tag_domain_id, tag_id, tag_text FROM active_tag_mappings_parents NATURAL JOIN tags WHERE "
		"record_id = $1",
		record_id ) };
	const auto display_rows { co_await display_tags };
	appendTagsByDomain( tags, KEY_DISPLAY_TAGS, display_rows );

	for ( const auto& service : services )
	{
		const auto service_key { service[ "service_key" ].asString() };

		if ( !tags.isMember( service_key ) ) continue;

		Json::Value& service_tags { tags[ service_key ] };

		service_tags[ KEY_NAME ] = service[ "name" ];
		service_tags[ KEY_TYPE_PRETTY ] = service[ "type_pretty" ];
		service_tags[ KEY_TYPE ] = service[ "type" ];
	}

	co_return data;
}

drogon::Task< drogon::HttpResponsePtr > HydrusAPI::fileMetadata( drogon::HttpRequestPtr request )
{
	logging::ScopedTimer timer { "fileMetadata", std::chrono::milliseconds( 250 ) };
	auto db { drogon::app().getDbClient() };

	if ( auto hashes_opt = request->getOptionalParameter< std::string >( "hashes" ) )
	{
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

	// Hydrus reports these twice: 'services' keyed by service key, and 'services_v2' as an array with
	// the key inside each entry. The per-record decoration below wants the array form.
	const auto service_infos { co_await listServices( db ) };
	const auto services { servicesList( service_infos ) };

	const auto hash_result { co_await db->execSqlCoro(
		"SELECT record_id, sha256, coalesce(size, 0), coalesce(mime.name, '') as mime_name, coalesce(coalesce(extension, "
		"best_extension), '') as extension, cluster_store_time, cluster_id FROM records LEFT JOIN file_info USING "
		"(record_id) LEFT "
		"JOIN mime USING (mime_id) WHERE record_id = ANY($1::" RECORD_PG_TYPE_NAME "[])",
		std::move( record_ids ) ) };

	struct Info
	{
		RecordID record_id;
		SHA256 sha256;
		std::size_t size;
		std::string mime_name;
		std::string extension;
	};

	Json::Value out {};
	out[ KEY_METADATA ] = std::move( metadata_json );

	Json::Value& metadata_out { out[ KEY_METADATA ] };

	const std::size_t row_count { hash_result.size() };

	for ( std::size_t offset { 0 }; offset < row_count; offset += METADATA_BATCH_SIZE )
	{
		const auto batch_end { std::min( offset + METADATA_BATCH_SIZE, row_count ) };

		if constexpr ( METADATA_BATCH_SIZE == 1 )
		{
			auto result { co_await getMetadataFromRow( db, services, hash_result[ offset ] ) };
			if ( !result ) co_return result.error();

			metadata_out.append( std::move( *result ) );
		}
		else
		{
			std::vector< drogon::Task< std::expected< Json::Value, drogon::HttpResponsePtr > > > tasks {};
			tasks.reserve( batch_end - offset );

			for ( std::size_t index { offset }; index < batch_end; ++index )
			{
				tasks.emplace_back( getMetadataFromRow( db, services, hash_result[ index ] ) );
			}

			auto when_all_awaiter { drogon::when_all( std::move( tasks ) ) };
			auto values { co_await when_all_awaiter };

			for ( auto& json_value : values )
			{
				if ( !json_value ) co_return json_value.error();

				metadata_out.append( std::move( *json_value ) );
			}
		}
	}

	out[ KEY_SERVICES ] = servicesDict( service_infos );
	out[ KEY_SERVICES_V2 ] = services;

	co_return drogon::HttpResponse::newHttpJsonResponse( std::move( out ) );
}
} // namespace idhan::hyapi
