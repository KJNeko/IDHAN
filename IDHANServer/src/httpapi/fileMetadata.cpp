#include "fileMetadata.hpp"

#include <folly/io/IOBuf.h>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <simdjson.h>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "apiAuth.hpp"
#include "collectRecordInfo.hpp"
#include "filesystem/PathHelpers.hpp"
#include "filesystem/recordFilePath.hpp"
#include "idValidation.hpp"
#include "idhan/SHA256.hpp"
#include "idhan/db/DBContext.hpp"
#include "jsonBody.hpp"
#include "metadata/RecordMetadata.hpp"
#include "modules/ModuleLoader.hpp"
#include "recordValidator.hpp"
#include "records.hpp"

namespace idhan::httpapi
{

constexpr std::size_t max_record_ids { 1000 };

static void serializeEntry( simdjson::builder::string_builder& builder, const metadata::RecordInfoEntry& entry )
{
	const auto field = [ &builder ]( const std::string_view key, const auto& value )
	{
		builder.append_comma();
		builder.append_key_value( key, value );
	};

	builder.start_object();
	builder.append_key_value( "record_id", entry.record_id );

	builder.append_comma();
	builder.escape_and_append_with_quotes( "hashes" );
	builder.append_colon();
	builder.start_object();
	builder.append_key_value( "sha256", entry.sha256_hex );
	builder.end_object();

	builder.append_comma();
	builder.escape_and_append_with_quotes( "archive_ids" );
	builder.append_colon();
	builder.start_array();
	bool first_archive { true };
	for ( const auto archive_id : entry.archive_ids )
	{
		if ( !first_archive ) builder.append_comma();
		builder.append( archive_id );
		first_archive = false;
	}
	builder.end_array();

	builder.append_comma();
	builder.escape_and_append_with_quotes( "tags" );
	builder.append_colon();
	builder.start_array();
	bool first_tag { true };
	for ( const auto& tag : entry.tags )
	{
		if ( !first_tag ) builder.append_comma();
		first_tag = false;
		builder.start_object();
		builder.append_key_value( "domain", tag.domain );
		builder.append_comma();
		builder.append_key_value( "text", tag.text );
		builder.end_object();
	}
	builder.end_array();

	builder.append_comma();
	builder.escape_and_append_with_quotes( "urls" );
	builder.append_colon();
	builder.start_array();
	bool first_url { true };
	for ( const auto& url : entry.urls )
	{
		if ( !first_url ) builder.append_comma();
		first_url = false;
		builder.append( url );
	}
	builder.end_array();

	if ( entry.size ) field( "size", *entry.size );
	if ( entry.mime ) field( "mime", *entry.mime );
	if ( entry.extension ) field( "extension", *entry.extension );
	if ( entry.file_mtime ) field( "file_mtime", *entry.file_mtime );
	if ( entry.file_ctime ) field( "file_ctime", *entry.file_ctime );

	if ( entry.parsed )
	{
		field( "parsed", *entry.parsed );

		if ( *entry.parsed )
		{
			if ( entry.simple_type ) field( "simple_type", *entry.simple_type );

			builder.append_comma();
			builder.escape_and_append_with_quotes( "extra" );
			builder.append_colon();
			if ( entry.extra_json )
				builder.append_raw( *entry.extra_json );
			else
				builder.append_raw( "{}" );

			if ( entry.width ) field( "width", *entry.width );
			if ( entry.height ) field( "height", *entry.height );
			if ( entry.channels ) field( "channels", *entry.channels );
			if ( entry.phash_hex ) field( "phash", *entry.phash_hex );
			if ( entry.has_exif ) field( "has_exif", *entry.has_exif );
			if ( entry.has_gps ) field( "has_gps", *entry.has_gps );
			if ( entry.has_xmp ) field( "has_xmp", *entry.has_xmp );
			if ( entry.has_iptc ) field( "has_iptc", *entry.has_iptc );
			if ( entry.has_icc_profile ) field( "has_icc_profile", *entry.has_icc_profile );
			if ( entry.lossless ) field( "lossless", *entry.lossless );
			if ( entry.quality ) field( "quality", *entry.quality );
			if ( entry.chroma_subsampling ) field( "chroma_subsampling", *entry.chroma_subsampling );
			if ( entry.progressive ) field( "progressive", *entry.progressive );
			if ( entry.comment ) field( "comment", *entry.comment );
			if ( entry.layers ) field( "layers", *entry.layers );
			if ( entry.duration ) field( "duration", *entry.duration );
			if ( entry.bitrate ) field( "bitrate", *entry.bitrate );
			if ( entry.framerate ) field( "framerate", *entry.framerate );
			if ( entry.has_audio ) field( "has_audio", *entry.has_audio );
			if ( entry.sample_rate ) field( "sample_rate", *entry.sample_rate );
			if ( entry.frame_count ) field( "frame_count", *entry.frame_count );
			if ( entry.loops ) field( "loops", *entry.loops );
		}
	}

	if ( entry.archive_id ) field( "archive_id", *entry.archive_id );
	if ( entry.encrypted ) field( "encrypted", *entry.encrypted );
	if ( entry.file_count ) field( "file_count", *entry.file_count );

	builder.end_object();
}

static http::HTTPTask< http::Response > fetchRecordInfo( db::DBContext& db, http::RequestContext context )
{
	const auto record_id_e { context.parameters().scalar< RecordID >( "record_id" ) };
	if ( !record_id_e ) co_return http::Response::error( 400, record_id_e.error().message );

	auto batch { co_await metadata::collectRecordInfo( { *record_id_e }, db ) };

	if ( batch.records.empty() ) co_return http::Response::error( 404, "Record ID was not found" );

	simdjson::builder::string_builder builder;
	serializeEntry( builder, batch.records.front() );

	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( *json ) );
}

static http::HTTPTask< http::Response > fetchRecordMetadataBatch( db::DBContext& db, http::RequestContext context )
{
	auto body { readValidatedJsonBody( context ) };
	if ( !body ) co_return std::move( body.error() );

	simdjson::dom::parser parser;
	const std::string_view body_text { *body };
	simdjson::dom::element root {};
	try
	{
		root = parser.parse( body_text.data(), body_text.size() );
	}
	catch ( const simdjson::simdjson_error& e )
	{
		co_return http::Response::error( 400, e.what() );
	}

	if ( !root.is_object() ) co_return http::Response::error( 400, "Request body must be a JSON object" );

	simdjson::dom::array record_ids_array {};
	if ( root[ "record_ids" ].get( record_ids_array ) != simdjson::SUCCESS )
		co_return http::Response::error( 400, "'record_ids' must be an array of integers" );

	if ( record_ids_array.size() > max_record_ids )
		co_return http::Response::error(
			400, std::format( "Too many record_ids: {} (max {})", record_ids_array.size(), max_record_ids ) );

	std::vector< RecordID > record_ids {};
	record_ids.reserve( record_ids_array.size() );
	for ( const simdjson::dom::element id_element : record_ids_array )
	{
		auto id { parsePositiveId< RecordID >( id_element, "record_id" ) };
		if ( !id ) co_return std::move( id.error() );

		record_ids.emplace_back( *id );
	}

	auto batch { co_await metadata::collectRecordInfo( std::move( record_ids ), db ) };

	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.escape_and_append_with_quotes( "records" );
	builder.append_colon();
	builder.start_array();
	bool first_record { true };
	for ( const auto& entry : batch.records )
	{
		if ( !first_record ) builder.append_comma();
		serializeEntry( builder, entry );
		first_record = false;
	}
	builder.end_array();

	builder.append_comma();
	builder.escape_and_append_with_quotes( "missing" );
	builder.append_colon();
	builder.start_array();
	bool first_missing { true };
	for ( const auto record_id : batch.missing )
	{
		if ( !first_missing ) builder.append_comma();
		builder.append( record_id );
		first_missing = false;
	}
	builder.end_array();
	builder.end_object();

	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( *json ) );
}

http::HTTPTask< std::expected< filesystem::VerifiedRecordPath, http::Response > > recordFilePathForScan(
	db::DBContext& db,
	const RecordID record_id )
{
	auto resolved { co_await filesystem::findPresentRecordFilePath( db, record_id ) };
	if ( !resolved ) co_return std::unexpected( http::Response::error( 503, resolved.error() ) );

	if ( !*resolved )
		co_return std::unexpected(
			http::Response::error( 404, std::format( "Record {} is not stored in any cluster", record_id ) ) );

	co_return std::move( **resolved );
}

http::HTTPTask< std::expected< void, http::Response > > parseAndStoreRecordMetadata(
	db::DBContext& db,
	const RecordID record_id )
{
	db::DBBindings mime_bindings {};
	mime_bindings.bind( record_id );
	const auto mime_rows {
		co_await db.query( "SELECT mime_id FROM file_info WHERE record_id = $1", std::move( mime_bindings ) )
			.rows< std::optional< MimeID > >()
	};

	if ( mime_rows.empty() )
		co_return std::unexpected(
			http::Response::error(
				400,
				std::format(
					"Record {} does not exist or does not have any file info associated with it", record_id ) ) );

	const auto mime_id_opt { std::get< 0 >( mime_rows.row( 0 ) ) };
	if ( !mime_id_opt )
		co_return std::unexpected(
			http::Response::error(
				400,
				std::format(
					"Record {} does not have any mime associated with it, Cannot parse metadata", record_id ) ) );

	const auto mime_id { *mime_id_opt };
	if ( modules::ModuleLoader::instance().getParserFor( mime_id ).empty() )
		co_return std::unexpected(
			http::Response::error( 400, std::format( "No parser found for mime id {}", mime_id ) ) );

	auto path { co_await recordFilePathForScan( db, record_id ) };
	if ( !path ) co_return std::unexpected( std::move( path.error() ) );

	auto parsed {
		co_await idhan::metadata::parseAndStoreRecordMetadata( db, record_id, mime_id, path->cluster(), path->path() )
	};
	if ( !parsed ) co_return std::unexpected( http::Response::error( parsed.error().status, parsed.error().message ) );

	co_return {};
}

static http::HTTPTask< http::Response > scanRecordMetadata( db::DBContext& db, http::RequestContext context )
{
	const auto record_id_e { context.parameters().scalar< RecordID >( "record_id" ) };
	if ( !record_id_e ) co_return http::Response::error( 400, record_id_e.error().message );
	const auto record_id { *record_id_e };

	auto scanned { co_await parseAndStoreRecordMetadata( db, record_id ) };
	if ( !scanned ) co_return std::move( scanned.error() );

	auto batch { co_await metadata::collectRecordInfo( { record_id }, db ) };
	if ( batch.records.empty() ) co_return http::Response::error( 404, "Record ID was not found" );

	simdjson::builder::string_builder builder;
	serializeEntry( builder, batch.records.front() );

	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( *json ) );
}

http::RouteDefinition recordInfoRoute( db::DBContext& db, std::string path )
{
	auto handler = [ &db ]( http::RequestContext context ) -> http::HTTPTask< http::Response >
	{ co_return co_await fetchRecordInfo( db, std::move( context ) ); };

	return http::RouteDefinition {
		http::RouteMetadata {
			proxygen::HTTPMethod::GET,
			std::move( path ),
			{ recordIdPathParameter() },
			{ apiAuthValidation( db ), recordExistsValidation( db ) } },
		std::move( handler )
	};
}

http::RouteDefinition recordMetadataBatchRoute( db::DBContext& db )
{
	auto handler = [ &db ]( http::RequestContext context ) -> http::HTTPTask< http::Response >
	{ co_return co_await fetchRecordMetadataBatch( db, std::move( context ) ); };

	return http::RouteDefinition {
		http::RouteMetadata { proxygen::HTTPMethod::POST, "/records/metadata", {}, { apiAuthValidation( db ) } },
		std::move( handler )
	};
}

http::RouteDefinition recordMetadataScanRoute( db::DBContext& db )
{
	auto handler = [ &db ]( http::RequestContext context ) -> http::HTTPTask< http::Response >
	{ co_return co_await scanRecordMetadata( db, std::move( context ) ); };

	return http::RouteDefinition {
		http::RouteMetadata {
			proxygen::HTTPMethod::POST,
			"/records/{record_id}/metadata/scan",
			{ recordIdPathParameter() },
			{ apiAuthValidation( db ), recordExistsValidation( db ) } },
		std::move( handler )
	};
}

} // namespace idhan::httpapi
