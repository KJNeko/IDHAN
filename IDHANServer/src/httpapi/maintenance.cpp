#include "maintenance.hpp"

#include <folly/io/IOBuf.h>
#include <json/writer.h>

#include <cstdint>
#include <filesystem>
#include <format>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#include "IDHANTypes.hpp"
#include "MimeIDs.hpp"
#include "filesystem/ThumbnailCache.hpp"
#include "filesystem/clusters/ClusterManager.hpp"
#include "idhan/db/DBContext.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"
#include "idhan/http/request/TemporaryUpload.hpp"
#include "ipc/UniqueFd.hpp"
#include "logging/log.hpp"
#include "metadata/ArchiveEntries.hpp"
#include "mime/identifyMime.hpp"
#include "mime/prescan.hpp"
#include "modules/CallInput.hpp"
#include "modules/ModuleLoader.hpp"
#include "modules/RemoteModule.hpp"
#include "paths.hpp"

namespace idhan::httpapi
{

[[nodiscard]] static std::string toCompactJson( const Json::Value& value )
{
	Json::StreamWriterBuilder builder {};
	builder[ "indentation" ] = "";
	return Json::writeString( builder, value );
}

http::HTTPTask< http::JsonResult< SunburstResponse > > sunburst(
	db::DBContext& db,
	[[maybe_unused]] http::RequestContext context )
{
	const auto rows {
		co_await db
			.query(
				"SELECT t.table_name::text AS table_name, "
				"       pg_relation_size(quote_ident(t.table_name)::text) AS table_size, "
				"       i.indexname::text AS index_name, "
				"       pg_relation_size(i.indexname::regclass) AS index_size "
				"FROM information_schema.tables t "
				"LEFT JOIN pg_indexes i "
				"       ON i.tablename = t.table_name AND i.schemaname = current_schema() "
				"WHERE t.table_schema = current_schema() AND t.table_type = 'BASE TABLE' "
				"ORDER BY t.table_name" )
			.rows< std::string, std::int64_t, std::optional< std::string >, std::optional< std::int64_t > >()
	};

	std::vector< SunburstResponse::SunburstTable > tables {};
	for ( const auto& [ table_name, table_size, index_name, index_size ] : rows )
	{
		if ( tables.empty() || tables.back().name != table_name )
			tables.push_back( SunburstResponse::SunburstTable { table_name, table_size, {} } );

		if ( index_name && index_size )
			tables.back().indexes.push_back( SunburstResponse::SunburstIndex { *index_name, *index_size } );
	}

	co_return http::JsonResponse< SunburstResponse > { { "root", std::move( tables ) } };
}

http::HTTPTask< http::JsonResult< DatabaseStatsResponse > > databaseStats(
	db::DBContext& db,
	[[maybe_unused]] http::RequestContext context )
{
	const auto counts {
		co_await db
			.query(
				"SELECT (SELECT COUNT(*) FROM records)       AS records, "
				"       (SELECT COUNT(*) FROM tags)          AS tags, "

				"       (SELECT COALESCE(SUM(GREATEST(c.reltuples, 0)), 0)::bigint "
				"          FROM pg_inherits i "
				"          JOIN pg_class c ON c.oid = i.inhrelid "
				"         WHERE i.inhparent = to_regclass(current_schema() || '.tag_mappings')) AS mappings_est" )
			.rows< std::int64_t, std::int64_t, std::int64_t >()
	};

	const auto& [ records, tags, mappings_est ] { counts.row( 0 ) };
	const auto clusters { filesystem::ClusterManager::getInstance().list().size() };

	std::int64_t mappings { mappings_est };
	bool mappings_estimated { true };
	if ( mappings == 0 )
	{
		const auto exact { co_await db.query( "SELECT COUNT(*) AS n FROM tag_mappings" ).rows< std::int64_t >() };
		mappings = std::get< 0 >( exact.row( 0 ) );
		mappings_estimated = false;
	}

	co_return http::JsonResponse< DatabaseStatsResponse > { { records, tags, clusters, mappings, mappings_estimated } };
}

http::HTTPTask< http::JsonResult< IntegrityCheckResponse > > integrityCheck(
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

	co_return http::JsonResponse< IntegrityCheckResponse > {
		{ static_path.string(), static_exists, std::move( files ) }
	};
}

http::HTTPTask< http::JsonResult< PurgeThumbnailsResponse > > purgeThumbnails(
	[[maybe_unused]] db::DBContext& db,
	[[maybe_unused]] http::RequestContext context )
{
	log::info( "Starting thumbnail purge from: {}", getThumbnailsPath().string() );

	const auto deleted { filesystem::clearThumbnailCache( getThumbnailsPath() ) };
	if ( !deleted )
	{
		log::error( "Error purging thumbnails: {}", deleted.error() );
		co_return http::Response::error( 500, std::format( "Failed to purge thumbnails: {}", deleted.error() ) );
	}

	log::info( "Thumbnail purge complete. Deleted: {}", *deleted );

	co_return http::JsonResponse< PurgeThumbnailsResponse > {
		{ true, static_cast< std::uint64_t >( *deleted ), "Thumbnails purged successfully" }
	};
}

http::HTTPTask< http::JsonResult< MimeListResponse > > listMimes(
	db::DBContext& db,
	[[maybe_unused]] http::RequestContext context )
{
	const auto rows {
		co_await db.query( "SELECT mime_id, name FROM mime ORDER BY mime_id" ).rows< MimeID, std::string >()
	};

	MimeListResponse response;
	for ( const auto& [ mime_id, name ] : rows ) response.emplace( std::to_string( mime_id ), name );

	co_return http::JsonResponse< MimeListResponse > { std::move( response ) };
}

http::HTTPTask< http::JsonResult< ParseMimeResponse > > parseMime(
	[[maybe_unused]] db::DBContext& db,
	http::RequestContext context,
	http::Query< ParseMimeQuery > query )
{
	const auto content_type { context.message().getHeaders().getSingleOrEmpty( std::string { "Content-Type" } ) };
	if ( content_type != "application/octet-stream" )
		co_return http::Response::error(
			400, std::format( "Content type must be application/octet-stream, was {}", content_type ) );

	const auto filename { query.value.filename.value_or( "" ) };

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

	if ( mime_id == mime_ids::UNKNOWN )
		co_return http::JsonResponse< ParseMimeResponse > { UnrecognizedMimeResponse {} };

	ParsedMimeResponse response {
		true,
		std::string { mime_ids::mime_names.at( mime_id ) },
		mime_id,
		std::to_underlying( mime_ids::simpleType( mime_id ) ),
		{}
	};

	if ( !input_e.has_value() )
	{
		response.metadata_modules.emplace_back(
			MimeMetadataErrorResponse {
				{}, std::format( "Could not stage the request body for a module: {}", input_e.error() ) } );
	}
	else
	{
		const auto& input { input_e.value() };
		const auto metadata_modules { modules::ModuleLoader::instance().getParserFor( mime_id ) };
		response.metadata_modules.reserve( metadata_modules.size() );
		for ( const auto& metadata_module : metadata_modules )
		{
			const modules::RemoteCallData data { .input = input, .mime_id = mime_id, .extra = {}, .depth = 0 };
			const auto metadata_info_e { co_await metadata_module->parseFile( data ) };
			if ( !metadata_info_e.has_value() )
			{
				response.metadata_modules.emplace_back(
					MimeMetadataErrorResponse { std::string { metadata_module->name() }, metadata_info_e.error() } );
				continue;
			}

			const auto& metadata_info { metadata_info_e.value() };
			MimeMetadataResponse metadata_response {
				std::string { metadata_module->name() },
				{},
				{},
				http::JsonDocument { toCompactJson( metadata_info.m_extra ) }
			};
			const auto* archive { std::get_if< MetadataInfoArchive >( &metadata_info.m_metadata ) };
			if ( archive )
			{
				metadata_response.count.emplace( archive->contained_records.size() );
				metadata_response.uncompressed_size.emplace( archive->m_size );
			}

			response.metadata_modules.emplace_back( std::move( metadata_response ) );
		}
	}

	co_return http::JsonResponse< ParseMimeResponse > { std::move( response ) };
}

http::HTTPTask< http::EncodedResult< http::RequestBody, "image/webp" > > createThumbnail(
	[[maybe_unused]] db::DBContext& db,
	http::RequestContext context )
{
	const auto content_type { context.message().getHeaders().getSingleOrEmpty( std::string { "Content-Type" } ) };
	if ( content_type != "application/octet-stream" )
		co_return http::Response::error(
			400, std::format( "Content type must be octet-stream, was {}", content_type ) );

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

	const auto thumbnail_data {
		co_await thumbnailers.front()->createThumbnailFile( std::move( call_data ), 128, 128 )
	};
	if ( !thumbnail_data ) co_return http::Response::error( 500, thumbnail_data.error() );

	const auto& thumb_info { *thumbnail_data };
	co_return http::encodedResponse< http::RequestBody, "image/webp" >( http::Response::bytes(
		200,
		folly::IOBuf::copyBuffer( thumb_info.m_pixel_data.data(), thumb_info.m_pixel_data.size() ),
		"image/webp" ) );
}

} // namespace idhan::httpapi
