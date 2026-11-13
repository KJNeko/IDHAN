#include "fileMetadata.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
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
#include "idhan/SHA256.hpp"
#include "idhan/db/DBContext.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"
#include "metadata/RecordMetadata.hpp"
#include "modules/ModuleLoader.hpp"
#include "recordValidator.hpp"
#include "records.hpp"

namespace idhan::httpapi
{

constexpr std::size_t max_record_ids { 1000 };

RecordInfoResponse recordInfoResponse( metadata::RecordInfoEntry entry )
{
	RecordInfoResponse response {
		.record_id = entry.record_id,
		.hashes = { std::move( entry.sha256_hex ) },
		.archive_ids = std::move( entry.archive_ids ),
		.tags = {},
		.urls = std::move( entry.urls ),
	};
	response.tags.reserve( entry.tags.size() );
	for ( auto& tag : entry.tags ) response.tags.push_back( { std::move( tag.domain ), std::move( tag.text ) } );

	response.size = std::move( entry.size );
	response.mime = std::move( entry.mime );
	response.extension = std::move( entry.extension );
	response.file_mtime = std::move( entry.file_mtime );
	response.file_ctime = std::move( entry.file_ctime );
	response.parsed = std::move( entry.parsed );
	response.archive_id = std::move( entry.archive_id );
	response.encrypted = std::move( entry.encrypted );
	response.file_count = std::move( entry.file_count );

	if ( !response.parsed.value_or( false ) ) return response;

	response.simple_type = std::move( entry.simple_type );
	response.extra.emplace( http::JsonDocument { entry.extra_json.value_or( "{}" ) } );
	response.width = std::move( entry.width );
	response.height = std::move( entry.height );
	response.channels = std::move( entry.channels );
	response.phash = std::move( entry.phash_hex );
	response.has_exif = std::move( entry.has_exif );
	response.has_gps = std::move( entry.has_gps );
	response.has_xmp = std::move( entry.has_xmp );
	response.has_iptc = std::move( entry.has_iptc );
	response.has_icc_profile = std::move( entry.has_icc_profile );
	response.lossless = std::move( entry.lossless );
	response.quality = std::move( entry.quality );
	response.chroma_subsampling = std::move( entry.chroma_subsampling );
	response.progressive = std::move( entry.progressive );
	response.comment = std::move( entry.comment );
	response.layers = std::move( entry.layers );
	response.duration = std::move( entry.duration );
	response.bitrate = std::move( entry.bitrate );
	response.framerate = std::move( entry.framerate );
	response.has_audio = std::move( entry.has_audio );
	response.sample_rate = std::move( entry.sample_rate );
	response.frame_count = std::move( entry.frame_count );
	response.loops = std::move( entry.loops );
	return response;
}

http::HTTPTask< http::JsonResult< RecordInfoResponse > > fetchRecordInfo(
	db::DBContext& db,
	http::RequestContext context )
{
	const auto record_id_e { context.parameters().scalar< RecordID >( "record_id" ) };
	if ( !record_id_e ) co_return http::Response::error( 400, record_id_e.error().message );

	auto batch { co_await metadata::collectRecordInfo( { *record_id_e }, db ) };

	if ( batch.records.empty() ) co_return http::Response::error( 404, "Record ID was not found" );

	co_return http::JsonResponse< RecordInfoResponse > { recordInfoResponse( std::move( batch.records.front() ) ) };
}

http::HTTPTask< http::JsonResult< RecordInfoBatchResponse > > fetchRecordMetadataBatch(
	db::DBContext& db,
	http::JsonBody< FetchRecordMetadataBatchBody > body )
{
	if ( body.value.record_ids.size() > max_record_ids )
		co_return http::Response::error(
			400, std::format( "Too many record_ids: {} (max {})", body.value.record_ids.size(), max_record_ids ) );

	auto batch { co_await metadata::collectRecordInfo( std::move( body.value.record_ids ), db ) };
	RecordInfoBatchResponse response { {}, std::move( batch.missing ) };
	response.records.reserve( batch.records.size() );
	for ( auto& entry : batch.records ) response.records.push_back( recordInfoResponse( std::move( entry ) ) );

	co_return http::JsonResponse< RecordInfoBatchResponse > { std::move( response ) };
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

http::HTTPTask< http::JsonResult< RecordInfoResponse > > scanRecordMetadata(
	db::DBContext& db,
	http::RequestContext context )
{
	const auto record_id_e { context.parameters().scalar< RecordID >( "record_id" ) };
	if ( !record_id_e ) co_return http::Response::error( 400, record_id_e.error().message );
	const auto record_id { *record_id_e };

	auto scanned { co_await parseAndStoreRecordMetadata( db, record_id ) };
	if ( !scanned ) co_return std::move( scanned.error() );

	auto batch { co_await metadata::collectRecordInfo( { record_id }, db ) };
	if ( batch.records.empty() ) co_return http::Response::error( 404, "Record ID was not found" );

	co_return http::JsonResponse< RecordInfoResponse > { recordInfoResponse( std::move( batch.records.front() ) ) };
}

} // namespace idhan::httpapi
