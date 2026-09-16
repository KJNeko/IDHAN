#include "maintenance.hpp"

#include <folly/io/IOBuf.h>

#include <cstdint>
#include <filesystem>
#include <format>
#include <json/writer.h>
#include <simdjson.h>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#include "IDHANTypes.hpp"
#include "MimeIDs.hpp"
#include "apiAuth.hpp"
#include "filesystem/ThumbnailCache.hpp"
#include "idhan/db/DBContext.hpp"
#include "logging/log.hpp"
#include "metadata/ArchiveEntries.hpp"
#include "mime/identifyMime.hpp"
#include "mime/prescan.hpp"
#include "idhan/http/TemporaryUpload.hpp"
#include "ipc/UniqueFd.hpp"
#include "modules/CallInput.hpp"
#include "modules/ModuleLoader.hpp"
#include "modules/RemoteModule.hpp"
#include "paths.hpp"

namespace idhan::httpapi
{

[[nodiscard]] static http::Response jsonResponse( simdjson::builder::string_builder& builder, const std::uint16_t status = 200 )
{
	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	return http::Response::json( status, folly::IOBuf::copyBuffer( *json ) );
}

[[nodiscard]] static std::string toCompactJson( const Json::Value& value )
{
	Json::StreamWriterBuilder builder {};
	builder[ "indentation" ] = "";
	return Json::writeString( builder, value );
}

struct SunburstIndex
{
	std::string name;
	std::int64_t size;
};

struct SunburstTable
{
	std::string name;
	std::int64_t size;
	std::vector< SunburstIndex > indexes;
};

static http::HTTPTask< http::Response > sunburst( db::DBContext& db, [[maybe_unused]] http::RequestContext context )
{
	const auto rows { co_await db.query(
		                  "SELECT t.table_name, "
		                  "       pg_relation_size(quote_ident(t.table_name)::text) AS table_size, "
		                  "       i.indexname AS index_name, "
		                  "       pg_relation_size(i.indexname::regclass) AS index_size "
		                  "FROM information_schema.tables t "
		                  "LEFT JOIN pg_indexes i "
		                  "       ON i.tablename = t.table_name AND i.schemaname = current_schema() "
		                  "WHERE t.table_schema = current_schema() AND t.table_type = 'BASE TABLE' "
		                  "ORDER BY t.table_name" )
		                  .rows< std::string, std::int64_t, std::optional< std::string >, std::optional< std::int64_t > >() };

	std::vector< SunburstTable > tables {};
	for ( const auto& [ table_name, table_size, index_name, index_size ] : rows )
	{
		if ( tables.empty() || tables.back().name != table_name )
			tables.push_back( SunburstTable { table_name, table_size, {} } );

		if ( index_name && index_size ) tables.back().indexes.push_back( SunburstIndex { *index_name, *index_size } );
	}

	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.append_key_value( "name", "root" );
	builder.append_comma();
	builder.escape_and_append_with_quotes( "children" );
	builder.append_colon();
	builder.start_array();

	bool first_table { true };
	for ( const auto& table : tables )
	{
		if ( !first_table ) builder.append_comma();
		first_table = false;

		builder.start_object();
		builder.append_key_value( "name", table.name );
		builder.append_comma();
		builder.append_key_value( "value", table.size );
		builder.append_comma();
		builder.escape_and_append_with_quotes( "children" );
		builder.append_colon();
		builder.start_array();

		bool first_index { true };
		for ( const auto& index : table.indexes )
		{
			if ( !first_index ) builder.append_comma();
			first_index = false;

			builder.start_object();
			builder.append_key_value( "name", index.name );
			builder.append_comma();
			builder.append_key_value( "value", index.size );
			builder.end_object();
		}

		builder.end_array();
		builder.end_object();
	}

	builder.end_array();
	builder.end_object();

	co_return jsonResponse( builder );
}

static http::HTTPTask< http::Response > databaseStats( db::DBContext& db, [[maybe_unused]] http::RequestContext context )
{
	const auto counts { co_await db.query(
		                    "SELECT (SELECT COUNT(*) FROM records)       AS records, "
		                    "       (SELECT COUNT(*) FROM tags)          AS tags, "
		                    "       (SELECT COUNT(*) FROM file_clusters) AS clusters, "
		                    "       (SELECT COALESCE(SUM(GREATEST(c.reltuples, 0)), 0)::bigint "
		                    "          FROM pg_inherits i "
		                    "          JOIN pg_class c ON c.oid = i.inhrelid "
		                    "         WHERE i.inhparent = to_regclass(current_schema() || '.tag_mappings')) AS mappings_est" )
		                    .rows< std::int64_t, std::int64_t, std::int64_t, std::int64_t >() };

	const auto& [ records, tags, clusters, mappings_est ] { counts.row( 0 ) };

	std::int64_t mappings { mappings_est };
	bool mappings_estimated { true };
	if ( mappings == 0 )
	{
		const auto exact { co_await db.query( "SELECT COUNT(*) AS n FROM tag_mappings" ).rows< std::int64_t >() };
		mappings = std::get< 0 >( exact.row( 0 ) );
		mappings_estimated = false;
	}

	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.append_key_value( "records", records );
	builder.append_comma();
	builder.append_key_value( "tags", tags );
	builder.append_comma();
	builder.append_key_value( "clusters", clusters );
	builder.append_comma();
	builder.append_key_value( "mappings", mappings );
	builder.append_comma();
	builder.append_key_value( "mappings_estimated", mappings_estimated );
	builder.end_object();

	co_return jsonResponse( builder );
}

static http::HTTPTask< http::Response > integrityCheck(
	[[maybe_unused]] db::DBContext& db,
	[[maybe_unused]] http::RequestContext context )
{
	const auto static_path { getStaticPath() };
	const bool static_exists { std::filesystem::exists( static_path ) };

	std::vector< std::string > files {};
	const auto scan_root { static_path.parent_path() };
	if ( std::filesystem::exists( scan_root ) )
	{
		try
		{
			for ( const auto& file : std::filesystem::recursive_directory_iterator( scan_root ) )
				if ( file.is_regular_file() ) files.push_back( file.path().string() );
		}
		catch ( const std::filesystem::filesystem_error& e )
		{
			co_return http::Response::error( 500, std::format( "Failed to iterate static directory: {}", e.what() ) );
		}
	}

	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.append_key_value( "static", static_path.string() );
	builder.append_comma();
	builder.append_key_value( "static_exists", static_exists );
	builder.append_comma();
	builder.escape_and_append_with_quotes( "files" );
	builder.append_colon();
	builder.start_array();

	bool first { true };
	for ( const auto& file : files )
	{
		if ( !first ) builder.append_comma();
		first = false;
		builder.append( file );
	}

	builder.end_array();
	builder.end_object();

	co_return jsonResponse( builder );
}

static http::HTTPTask< http::Response > purgeThumbnails(
	[[maybe_unused]] db::DBContext& db,
	[[maybe_unused]] http::RequestContext context )
{
	log::info( "Starting thumbnail purge from: {}", getThumbnailsPath().string() );

	const auto deleted { filesystem::clearThumbnailCache() };
	if ( !deleted )
	{
		log::error( "Error purging thumbnails: {}", deleted.error() );
		co_return http::Response::error( 500, std::format( "Failed to purge thumbnails: {}", deleted.error() ) );
	}

	log::info( "Thumbnail purge complete. Deleted: {}", *deleted );

	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.append_key_value( "success", true );
	builder.append_comma();
	builder.append_key_value( "deleted_count", static_cast< std::uint64_t >( *deleted ) );
	builder.append_comma();
	builder.append_key_value( "message", "Thumbnails purged successfully" );
	builder.end_object();

	co_return jsonResponse( builder );
}

static http::HTTPTask< http::Response > listMimes( db::DBContext& db, [[maybe_unused]] http::RequestContext context )
{
	const auto rows { co_await db.query( "SELECT mime_id, name FROM mime ORDER BY mime_id" ).rows< MimeID, std::string >() };

	simdjson::builder::string_builder builder;
	builder.start_object();

	bool first { true };
	for ( const auto& [ mime_id, name ] : rows )
	{
		if ( !first ) builder.append_comma();
		first = false;
		builder.append_key_value( std::to_string( mime_id ), name );
	}

	builder.end_object();

	co_return jsonResponse( builder );
}

static http::HTTPTask< http::Response > parseMime( [[maybe_unused]] db::DBContext& db, http::RequestContext context )
{
	const auto content_type { context.message().getHeaders().getSingleOrEmpty( std::string { "Content-Type" } ) };
	if ( content_type != "application/octet-stream" )
		co_return http::Response::error(
			400, std::format( "Content type must be application/octet-stream, was {}", content_type ) );

	const auto filename_e { context.parameters().optional< std::string >( "filename" ) };
	if ( !filename_e ) co_return http::Response::error( 400, filename_e.error().message );
	const auto filename { filename_e->value_or( "" ) };

	auto body { std::move( context ).takeBody() };

	MimeID mime_id { mime_ids::UNKNOWN };
	std::expected< std::shared_ptr< const modules::CallInput >, std::string > input_e {};

	if ( auto* upload { std::get_if< http::TemporaryUpload >( &body ) } )
	{
		if ( upload->size() == 0 ) co_return http::Response::error( 400, "No data provided in POST request" );

		const auto probe { upload->duplicateFile() };
		mime_id = co_await mime::identifyMimeForFd( probe.fd(), upload->size(), filename );

		auto owned { modules::CallInput::forFd( ipc::UniqueFd { std::move( *upload ).releaseFile().release() } ) };
		if ( owned )
			input_e = std::make_shared< const modules::CallInput >( std::move( *owned ) );
		else
			input_e = std::unexpected( std::move( owned.error() ) );
	}
	else
	{
		const auto request_data { std::get< std::unique_ptr< folly::IOBuf > >( body )->to< std::string >() };
		if ( request_data.empty() ) co_return http::Response::error( 400, "No data provided in POST request" );

		const std::span< const std::byte > bytes {
			reinterpret_cast< const std::byte* >( request_data.data() ), request_data.size()
		};

		mime_id = co_await mime::identifyMime( bytes, filename );
		input_e = modules::CallInput::sharedForBytes( bytes );
	}

	simdjson::builder::string_builder builder;
	builder.start_object();

	if ( mime_id == mime_ids::UNKNOWN )
	{
		builder.append_key_value( "success", false );
		builder.append_comma();
		builder.append_key_value( "error", "Failed to parse mime type" );
		builder.end_object();
		co_return jsonResponse( builder );
	}

	builder.append_key_value( "success", true );
	builder.append_comma();
	builder.append_key_value( "mime", mime_ids::mime_names.at( mime_id ) );
	builder.append_comma();
	builder.append_key_value( "mime_id", mime_id );
	builder.append_comma();
	builder.append_key_value( "generic_mime_id", std::to_underlying( mime_ids::simpleType( mime_id ) ) );
	builder.append_comma();

	builder.escape_and_append_with_quotes( "metadata_modules" );
	builder.append_colon();
	builder.start_array();

	if ( !input_e )
	{
		builder.start_object();
		builder.append_key_value(
			"error", std::format( "Could not stage the request body for a module: {}", input_e.error() ) );
		builder.end_object();
	}
	else
	{
		const auto input { *input_e };
		const auto metadata_modules { modules::ModuleLoader::instance().getParserFor( mime_id ) };

		bool first { true };
		for ( const auto& metadata_module : metadata_modules )
		{
			if ( !first ) builder.append_comma();
			first = false;

			builder.start_object();
			builder.append_key_value( "name", metadata_module->name() );

			const modules::RemoteCallData data { .input = input, .mime_id = mime_id, .extra = {}, .depth = 0 };
			const auto metadata_info { co_await metadata_module->parseFile( data ) };

			if ( !metadata_info )
			{
				builder.append_comma();
				builder.append_key_value( "error", metadata_info.error() );
				builder.end_object();
				continue;
			}

			if ( const auto* archive { std::get_if< MetadataInfoArchive >( &metadata_info->m_metadata ) } )
			{
				builder.append_comma();
				builder.append_key_value( "count", archive->contained_records.size() );
				builder.append_comma();
				builder.append_key_value( "uncompressed_size", archive->m_size );
			}

			builder.append_comma();
			builder.escape_and_append_with_quotes( "extra" );
			builder.append_colon();
			builder.append_raw( toCompactJson( metadata_info->m_extra ) );

			builder.end_object();
		}
	}

	builder.end_array();
	builder.end_object();

	co_return jsonResponse( builder );
}

static http::HTTPTask< http::Response > createThumbnail( [[maybe_unused]] db::DBContext& db, http::RequestContext context )
{
	const auto content_type { context.message().getHeaders().getSingleOrEmpty( std::string { "Content-Type" } ) };
	if ( content_type != "application/octet-stream" )
		co_return http::Response::error( 400, std::format( "Content type must be octet-stream, was {}", content_type ) );

	auto body { std::move( context ).takeBody() };

	MimeID mime_id { mime_ids::UNKNOWN };
	std::expected< std::shared_ptr< const modules::CallInput >, std::string > input_e {};

	if ( auto* upload { std::get_if< http::TemporaryUpload >( &body ) } )
	{
		if ( upload->size() == 0 ) co_return http::Response::error( 400, "No data provided in POST request" );

		const auto probe { upload->duplicateFile() };
		mime_id = co_await mime::prescanMimeForFd( probe.fd(), upload->size() );

		auto owned { modules::CallInput::forFd( ipc::UniqueFd { std::move( *upload ).releaseFile().release() } ) };
		if ( owned )
			input_e = std::make_shared< const modules::CallInput >( std::move( *owned ) );
		else
			input_e = std::unexpected( std::move( owned.error() ) );
	}
	else
	{
		const auto request_data { std::get< std::unique_ptr< folly::IOBuf > >( body )->to< std::string >() };
		if ( request_data.empty() ) co_return http::Response::error( 400, "No data provided in POST request" );

		const std::span< const std::byte > bytes {
			reinterpret_cast< const std::byte* >( request_data.data() ), request_data.size()
		};

		mime_id = co_await mime::prescanMime( mime::MimeReader { request_data } );
		input_e = modules::CallInput::sharedForBytes( bytes );
	}

	if ( mime_id == mime_ids::UNKNOWN ) co_return http::Response::error( 400, "Failed to detect mime type" );

	const auto metadata_parser { modules::ModuleLoader::instance().getParserFor( mime_id ) };
	if ( metadata_parser.empty() )
		co_return http::Response::error( 500, std::format( "Was unable to find parser for mime id {}", mime_id ) );

	if ( !input_e )
		co_return http::Response::error(
			500, std::format( "Could not stage the request body for a module: {}", input_e.error() ) );

	const auto input { *input_e };

	modules::RemoteCallData call_data { .input = input, .mime_id = mime_id, .extra = {}, .depth = 0 };
	const auto metadata_json { co_await metadata_parser.front()->parseFile( call_data ) };
	if ( !metadata_json )
		co_return http::Response::error(
			500, std::format( "Unable to parse metadata for mime id {} reason: {}", mime_id, metadata_json.error() ) );

	call_data.extra = metadata_json->m_extra;

	if ( const auto* archive { std::get_if< MetadataInfoArchive >( &metadata_json->m_metadata ) } )
		metadata::applyArchiveEntries( call_data.extra, *archive );

	const auto thumbnailers { modules::ModuleLoader::instance().getThumbnailerFor( mime_id ) };
	if ( thumbnailers.empty() )
		co_return http::Response::error( 404, std::format( "No thumbnailer available for mime id {}", mime_id ) );

	const auto thumbnail_data { co_await thumbnailers.front()->createThumbnailFile( std::move( call_data ), 128, 128 ) };
	if ( !thumbnail_data ) co_return http::Response::error( 500, thumbnail_data.error() );

	const auto& thumb_info { *thumbnail_data };
	co_return http::Response::bytes(
		200,
		folly::IOBuf::copyBuffer( thumb_info.m_pixel_data.data(), thumb_info.m_pixel_data.size() ),
		"image/webp" );
}

std::vector< http::RouteDefinition > maintenanceRoutes( db::DBContext& db )
{
	std::vector< http::RouteDefinition > routes {};

	const auto add = [ &routes, &db ](
		                  const proxygen::HTTPMethod method,
		                  std::string path,
		                  http::HTTPTask< http::Response > ( *handler )( db::DBContext&, http::RequestContext ) )
	{
		auto bound = [ &db, handler ]( http::RequestContext context ) -> http::HTTPTask< http::Response >
		{ co_return co_await handler( db, std::move( context ) ); };

		routes.push_back( http::RouteDefinition {
			http::RouteMetadata { method, std::move( path ), {}, { apiAuthValidation( db ) } }, std::move( bound ) } );
	};

	add( proxygen::HTTPMethod::GET, "/db/stats/sunburst", sunburst );
	add( proxygen::HTTPMethod::GET, "/db/stats", databaseStats );
	add( proxygen::HTTPMethod::GET, "/integrity", integrityCheck );
	add( proxygen::HTTPMethod::POST, "/purge/thumbnails", purgeThumbnails );
	add( proxygen::HTTPMethod::GET, "/mime", listMimes );
	add( proxygen::HTTPMethod::POST, "/mime/parse", parseMime );
	add( proxygen::HTTPMethod::POST, "/mime/generate_thumbnail", createThumbnail );

	return routes;
}

}
