#include "fileMetadata.hpp"

#include <folly/io/IOBuf.h>

#include <bitset>
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

#include "MetadataInfo.hpp"
#include "apiAuth.hpp"
#include "collectRecordInfo.hpp"
#include "filesystem/PathHelpers.hpp"
#include "idhan/SHA256.hpp"
#include "idhan/db/DBContext.hpp"
#include "modules/CallInput.hpp"
#include "modules/ModuleLoader.hpp"
#include "modules/RemoteModule.hpp"
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
	auto body { std::move( context ).takeBody() };
	if ( !std::holds_alternative< std::unique_ptr< folly::IOBuf > >( body ) )
		co_return http::Response::error( 400, "Request body is too large" );

	const auto text { std::get< std::unique_ptr< folly::IOBuf > >( body )->to< std::string >() };

	const auto validated { http::ValidatedJson::parse( text ) };
	if ( !validated ) co_return http::Response::error( 400, validated.error().message );

	simdjson::dom::parser parser;
	const auto body_text { validated->text() };
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
		std::int64_t id {};
		if ( id_element.get_int64().get( id ) != simdjson::SUCCESS )
			co_return http::Response::error( 400, "'record_ids' must be an array of integers" );
		record_ids.emplace_back( static_cast< RecordID >( id ) );
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

[[nodiscard]] static std::bitset< 64 > phashToBitset( const PerceptualHash& phash )
{
	std::uint64_t value {};
	for ( const auto byte : phash ) value = ( value << 8 ) | std::to_integer< std::uint64_t >( byte );
	return std::bitset< 64 > { value };
}

http::HTTPTask< std::expected< std::filesystem::path, http::Response > > recordFilePathForScan(
	db::DBContext& db,
	const RecordID record_id )
{
	db::DBBindings bindings {};
	bindings.bind( record_id );
	const auto rows {
		co_await db
			.query(
				"SELECT folder_path, sha256, COALESCE(extension, best_extension, '') AS extension "
				"FROM records "
				"JOIN file_info ON records.record_id = file_info.record_id "
				"LEFT JOIN mime ON file_info.mime_id = mime.mime_id "
				"JOIN file_clusters ON file_clusters.cluster_id = file_info.cluster_id "
				"WHERE records.record_id = $1",
				std::move( bindings ) )
			.rows< std::string, std::span< const std::byte >, std::string >()
	};

	if ( rows.empty() )
		co_return std::unexpected(
			http::Response::error( 404, std::format( "Record {} is not stored in any cluster", record_id ) ) );

	const auto& [ folder_path, sha256_bytes, extension ] { rows.row( 0 ) };

	const SHA256 sha256 { SHA256::fromBuffer( std::vector< std::byte > { sha256_bytes.begin(), sha256_bytes.end() } ) };
	const std::filesystem::path file_location {
		std::filesystem::path { folder_path } / filesystem::getClusterRelativePath( sha256, extension )
	};

	std::error_code status_error {};
	const auto file_status { std::filesystem::status( file_location, status_error ) };

	if ( status_error == std::errc::no_such_file_or_directory )
	{
		db::DBBindings missing_bindings {};
		missing_bindings.bind( record_id );
		co_await db
			.query(
				"UPDATE file_info SET cluster_id = NULL WHERE record_id = $1 AND cluster_delete_time IS NULL",
				std::move( missing_bindings ) )
			.result();
		co_return std::unexpected(
			http::Response::error(
				500,
				std::format(
					"Record {} does not exist at the expected path '{}'.", record_id, file_location.string() ) ) );
	}

	if ( status_error )
		co_return std::unexpected(
			http::Response::error(
				500,
				std::format(
					"Could not inspect the expected path for record {} at '{}': {}",
					record_id,
					file_location.string(),
					status_error.message() ) ) );

	if ( !std::filesystem::is_regular_file( file_status ) )
		co_return std::unexpected(
			http::Response::error(
				500,
				std::format(
					"Expected path for record {} is not a regular file: '{}'.", record_id, file_location.string() ) ) );

	co_return file_location;
}

static http::HTTPTask< void > updateRecordFileTimesForScan(
	db::DBContext& db,
	const RecordID record_id,
	const std::filesystem::path& path )
{
	const auto times { filesystem::getFileTimes( path ) };
	if ( !times ) co_return;

	const std::int64_t mtime_us { filesystem::toMicroseconds( times->mtime ) };
	const std::optional< std::int64_t > ctime_us { filesystem::toMicroseconds( times->btime ) };

	db::DBBindings bindings {};
	bindings.bind( mtime_us );
	bindings.bind( ctime_us );
	bindings.bind( record_id );
	co_await db
		.query(
			"UPDATE file_info SET file_mtime = TIMESTAMP 'epoch' + $1::bigint * INTERVAL '1 microsecond', "
			"file_ctime = TIMESTAMP 'epoch' + $2::bigint * INTERVAL '1 microsecond' WHERE record_id = $3",
			std::move( bindings ) )
		.result();
}

[[nodiscard]] static http::HTTPTask< std::expected< void, http::Response > > writeRecordMetadata(
	db::DBContext& db,
	const RecordID record_id,
	const MetadataInfo& metadata )
{
	const auto simple_type { metadata.m_simple_type };

	if ( metadata.m_extra.isNull() )
	{
		db::DBBindings bindings {};
		bindings.bind( record_id );
		bindings.bind( static_cast< SmallInt >( std::to_underlying( simple_type ) ) );
		co_await db
			.query(
				"INSERT INTO metadata (record_id, simple_mime_type) VALUES ($1, $2) "
				"ON CONFLICT (record_id) DO UPDATE SET simple_mime_type = $2",
				std::move( bindings ) )
			.result();
	}
	else
	{
		db::DBBindings bindings {};
		bindings.bind( record_id );
		bindings.bind( static_cast< SmallInt >( std::to_underlying( simple_type ) ) );
		bindings.bind( metadata.m_extra.toStyledString() );
		co_await db
			.query(
				"INSERT INTO metadata (record_id, simple_mime_type, json) VALUES ($1, $2, $3::json) "
				"ON CONFLICT (record_id) DO UPDATE SET simple_mime_type = $2, json = $3::json",
				std::move( bindings ) )
			.result();
	}

	switch ( simple_type )
	{
		case SimpleMimeType::IMAGE_PROJECT:
			{
				const auto& project_metadata { std::get< MetadataInfoImageProject >( metadata.m_metadata ) };
				db::DBBindings bindings {};
				bindings.bind( record_id );
				bindings.bind( static_cast< std::int32_t >( project_metadata.image_info.width ) );
				bindings.bind( static_cast< std::int32_t >( project_metadata.image_info.height ) );
				bindings.bind( static_cast< SmallInt >( project_metadata.image_info.channels ) );
				bindings.bind( static_cast< SmallInt >( project_metadata.layers ) );
				co_await db
					.query(
						"INSERT INTO image_project_metadata (record_id, width, height, channels, layers) "
						"VALUES ($1, $2, $3, $4, $5) "
						"ON CONFLICT (record_id) DO UPDATE SET width = $2, height = $3, channels = $4, layers = $5",
						std::move( bindings ) )
					.result();
				break;
			}
		case SimpleMimeType::IMAGE_TYPE:
			{
				const auto& image_metadata { std::get< MetadataInfoImage >( metadata.m_metadata ) };

				std::optional< std::bitset< 64 > > phash {};
				if ( image_metadata.phash ) phash = phashToBitset( *image_metadata.phash );

				const auto& embedded { image_metadata.embedded };
				const auto& encoding { image_metadata.encoding };

				std::optional< bool > lossless {};
				std::optional< SmallInt > quality {};
				std::optional< SmallInt > subsampling {};
				std::optional< bool > progressive {};

				if ( encoding )
				{
					lossless = encoding->lossless;
					subsampling = static_cast< SmallInt >( std::to_underlying( encoding->subsampling ) );
					progressive = encoding->progressive;
					if ( encoding->quality ) quality = static_cast< SmallInt >( *encoding->quality );
				}

				db::DBBindings bindings {};
				bindings.bind( record_id );
				bindings.bind( static_cast< std::int32_t >( image_metadata.width ) );
				bindings.bind( static_cast< std::int32_t >( image_metadata.height ) );
				bindings.bind( static_cast< SmallInt >( image_metadata.channels ) );
				bindings.bind( phash );
				bindings.bind( embedded.exif );
				bindings.bind( embedded.gps );
				bindings.bind( embedded.xmp );
				bindings.bind( embedded.iptc );
				bindings.bind( embedded.icc_profile );
				bindings.bind( lossless );
				bindings.bind( quality );
				bindings.bind( subsampling );
				bindings.bind( progressive );
				bindings.bind( image_metadata.comment );

				co_await db
					.query(
						"INSERT INTO image_metadata (record_id, width, height, channels, phash, has_exif, has_gps, has_xmp, "
						"has_iptc, has_icc_profile, lossless, quality, subsampling, progressive, comment) "
						"VALUES ($1, $2, $3, $4, $5::bit(64), $6, $7, $8, $9, $10, $11, $12, $13, $14, $15) "
						"ON CONFLICT (record_id) DO UPDATE SET width = $2, height = $3, channels = $4, phash = $5::bit(64), "
						"has_exif = $6, has_gps = $7, has_xmp = $8, has_iptc = $9, has_icc_profile = $10, "
						"lossless = $11, quality = $12, subsampling = $13, progressive = $14, comment = $15",
						std::move( bindings ) )
					.result();
				break;
			}
		case SimpleMimeType::VIDEO:
			{
				const auto& video_metadata { std::get< MetadataInfoVideo >( metadata.m_metadata ) };
				db::DBBindings bindings {};
				bindings.bind( record_id );
				bindings.bind( static_cast< std::int32_t >( video_metadata.width ) );
				bindings.bind( static_cast< std::int32_t >( video_metadata.height ) );
				bindings.bind( static_cast< std::int32_t >( video_metadata.bitrate_bps ) );
				bindings.bind( video_metadata.duration_s );
				bindings.bind( video_metadata.fps );
				bindings.bind( video_metadata.has_audio );
				co_await db
					.query(
						"INSERT INTO video_metadata (record_id, width, height, bitrate, duration, framerate, has_audio) "
						"VALUES ($1, $2, $3, $4, $5, $6, $7) "
						"ON CONFLICT (record_id) DO UPDATE SET width = $2, height = $3, bitrate = $4, duration = $5, "
						"framerate = $6, has_audio = $7",
						std::move( bindings ) )
					.result();
				break;
			}
		case SimpleMimeType::ARCHIVE:
			{
				const auto& archive_metadata { std::get< MetadataInfoArchive >( metadata.m_metadata ) };

				std::vector< RecordID > records {};
				std::vector< std::string > paths {};
				records.reserve( archive_metadata.contained_records.size() );
				paths.reserve( archive_metadata.contained_records.size() );

				for ( const auto& [ record_sha256, path ] : archive_metadata.contained_records )
				{
					const SHA256 sha256 {
						SHA256::fromBuffer( std::vector< std::byte > { record_sha256.begin(), record_sha256.end() } )
					};

					auto contained_record_id { co_await createRecord( sha256, db ) };
					if ( !contained_record_id ) co_return std::unexpected( std::move( contained_record_id.error() ) );

					records.emplace_back( *contained_record_id );
					paths.emplace_back( path );
				}

				db::DBBindings archive_lookup_bindings {};
				archive_lookup_bindings.bind( record_id );
				const auto existing_metadata {
					co_await db
						.query(
							"SELECT archive_id FROM archive_metadata WHERE record_id = $1",
							std::move( archive_lookup_bindings ) )
						.rows< std::int32_t >()
				};

				std::int32_t archive_id { 0 };
				if ( !existing_metadata.empty() ) archive_id = std::get< 0 >( existing_metadata.row( 0 ) );

				if ( archive_id == 0 )
				{
					const auto inserted_archive_id {
						co_await db.query( "INSERT INTO archives DEFAULT VALUES RETURNING archive_id" )
							.rows< std::int32_t >()
					};
					archive_id = std::get< 0 >( inserted_archive_id.row( 0 ) );
				}

				db::DBBindings delete_bindings {};
				delete_bindings.bind( archive_id );
				delete_bindings.bind( paths );
				co_await db
					.query(
						"DELETE FROM archive_map WHERE archive_id = $1 AND path <> ALL($2::text[])",
						std::move( delete_bindings ) )
					.result();

				if ( !records.empty() )
				{
					db::DBBindings map_bindings {};
					map_bindings.bind( archive_id );
					map_bindings.bind( records );
					map_bindings.bind( paths );
					co_await db
						.query(
							"INSERT INTO archive_map (archive_id, record_id, path) "
							"SELECT $1, entry.record_id, entry.path "
							"FROM UNNEST($2::" RECORD_PG_TYPE_NAME "[], $3::text[]) AS entry(record_id, path) "
							"ON CONFLICT (archive_id, path) DO UPDATE SET record_id = EXCLUDED.record_id",
							std::move( map_bindings ) )
						.result();
				}

				db::DBBindings archive_metadata_bindings {};
				archive_metadata_bindings.bind( record_id );
				archive_metadata_bindings.bind( archive_id );
				archive_metadata_bindings.bind( archive_metadata.encrypted );
				co_await db
					.query(
						"INSERT INTO archive_metadata (record_id, archive_id, encrypted) VALUES ($1, $2, $3) "
						"ON CONFLICT (record_id) DO UPDATE SET encrypted = $3",
						std::move( archive_metadata_bindings ) )
					.result();

				break;
			}
		case SimpleMimeType::ANIMATION:
			{
				const auto& animation_metadata { std::get< MetadataInfoAnimation >( metadata.m_metadata ) };
				db::DBBindings bindings {};
				bindings.bind( record_id );
				bindings.bind( static_cast< std::int32_t >( animation_metadata.width ) );
				bindings.bind( static_cast< std::int32_t >( animation_metadata.height ) );
				bindings.bind( static_cast< std::int32_t >( animation_metadata.frame_count ) );
				bindings.bind( animation_metadata.duration_s );
				bindings.bind( animation_metadata.loops );
				co_await db
					.query(
						"INSERT INTO animation_metadata (record_id, width, height, frame_count, duration, loops) "
						"VALUES ($1, $2, $3, $4, $5, $6) "
						"ON CONFLICT (record_id) DO UPDATE SET width = $2, height = $3, frame_count = $4, duration = $5, "
						"loops = $6",
						std::move( bindings ) )
					.result();
				break;
			}
		case SimpleMimeType::AUDIO:
			{
				const auto& audio_metadata { std::get< MetadataInfoAudio >( metadata.m_metadata ) };
				db::DBBindings bindings {};
				bindings.bind( record_id );
				bindings.bind( audio_metadata.duration_s );
				bindings.bind( static_cast< std::int32_t >( audio_metadata.bitrate_bps ) );
				bindings.bind( static_cast< SmallInt >( audio_metadata.channels ) );
				bindings.bind( static_cast< std::int32_t >( audio_metadata.sample_rate ) );
				co_await db
					.query(
						"INSERT INTO audio_metadata (record_id, duration, bitrate, channels, sample_rate) "
						"VALUES ($1, $2, $3, $4, $5) "
						"ON CONFLICT (record_id) DO UPDATE SET duration = $2, bitrate = $3, channels = $4, sample_rate = $5",
						std::move( bindings ) )
					.result();
				break;
			}
		case SimpleMimeType::NONE:
			break;
	}

	co_return {};
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

	const auto parsers { modules::ModuleLoader::instance().getParserFor( mime_id ) };
	if ( parsers.empty() )
		co_return std::unexpected(
			http::Response::error( 400, std::format( "No parser found for mime id {}", mime_id ) ) );

	const auto& parser { parsers.front() };

	auto path_e { co_await recordFilePathForScan( db, record_id ) };
	if ( !path_e ) co_return std::unexpected( std::move( path_e.error() ) );

	auto input_e { modules::CallInput::forPath( *path_e ) };
	if ( !input_e )
		co_return std::unexpected(
			http::Response::error(
				500, std::format( "Failed to open the file for record {}: {}", record_id, input_e.error() ) ) );

	const auto input { std::make_shared< const modules::CallInput >( std::move( *input_e ) ) };

	const modules::RemoteCallData call_data { .input = input, .mime_id = mime_id, .extra = {}, .depth = 0 };
	const auto metadata_e { co_await parser->parseFile( call_data ) };

	if ( !metadata_e )
		co_return std::unexpected(
			http::Response::error(
				500, std::format( "Module failed to parse data for record {}: {}", record_id, metadata_e.error() ) ) );

	auto written { co_await writeRecordMetadata( db, record_id, *metadata_e ) };
	if ( !written ) co_return std::unexpected( std::move( written.error() ) );

	co_await updateRecordFileTimesForScan( db, record_id, *path_e );

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
