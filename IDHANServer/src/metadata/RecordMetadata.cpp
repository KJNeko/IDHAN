#include "RecordMetadata.hpp"

#include <bitset>
#include <format>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "MetadataInfo.hpp"
#include "db/records.hpp"
#include "filesystem/PathHelpers.hpp"
#include "filesystem/clusters/ClusterAccess.hpp"
#include "idhan/SHA256.hpp"
#include "idhan/db/DBContext.hpp"
#include "idhan/db/DBTransaction.hpp"
#include "modules/CallInput.hpp"
#include "modules/ModuleLoader.hpp"
#include "modules/RemoteModule.hpp"

namespace idhan::metadata
{

[[nodiscard]] static std::bitset< 64 > phashToBitset( const PerceptualHash& phash )
{
	std::uint64_t value {};
	for ( const auto byte : phash ) value = ( value << 8 ) | std::to_integer< std::uint64_t >( byte );
	return std::bitset< 64 > { value };
}

static runtime::Task< void > updateRecordFileTimesForScan(
	db::DBTransaction& db,
	const RecordID record_id,
	const std::filesystem::path& path )
{
	const auto times { filesystem::getFileTimes( path ) };
	if ( !times ) co_return;

	const std::int64_t mtime_us { filesystem::toMicroseconds( times->mtime ) };
	const std::optional< std::int64_t > ctime_us { filesystem::toMicroseconds( times->btime ) };

	static constexpr char query[] {
#embed "sql/update_record_file_times.sql" suffix(, )
		'\0'
	};

	db::DBBindings bindings {};
	bindings.bind( mtime_us );
	bindings.bind( ctime_us );
	bindings.bind( record_id );
	co_await db.query( query, std::move( bindings ) ).result();
}

[[nodiscard]] static runtime::Task< std::expected< void, MetadataError > > writeRecordMetadata(
	db::DBTransaction& db,
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
				static constexpr char query[] {
#embed "sql/store_image_project_metadata.sql" suffix(, )
					'\0'
				};

				const auto& project_metadata { std::get< MetadataInfoImageProject >( metadata.m_metadata ) };
				db::DBBindings bindings {};
				bindings.bind( record_id );
				bindings.bind( static_cast< std::int32_t >( project_metadata.image_info.width ) );
				bindings.bind( static_cast< std::int32_t >( project_metadata.image_info.height ) );
				bindings.bind( static_cast< SmallInt >( project_metadata.image_info.channels ) );
				bindings.bind( static_cast< SmallInt >( project_metadata.layers ) );
				co_await db.query( query, std::move( bindings ) ).result();
				break;
			}
		case SimpleMimeType::IMAGE_TYPE:
			{
				static constexpr char query[] {
#embed "sql/store_image_metadata.sql" suffix(, )
					'\0'
				};

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

				co_await db.query( query, std::move( bindings ) ).result();
				break;
			}
		case SimpleMimeType::VIDEO:
			{
				static constexpr char query[] {
#embed "sql/store_video_metadata.sql" suffix(, )
					'\0'
				};

				const auto& video_metadata { std::get< MetadataInfoVideo >( metadata.m_metadata ) };
				db::DBBindings bindings {};
				bindings.bind( record_id );
				bindings.bind( static_cast< std::int32_t >( video_metadata.width ) );
				bindings.bind( static_cast< std::int32_t >( video_metadata.height ) );
				bindings.bind( static_cast< std::int32_t >( video_metadata.bitrate_bps ) );
				bindings.bind( video_metadata.duration_s );
				bindings.bind( video_metadata.fps );
				bindings.bind( video_metadata.has_audio );
				co_await db.query( query, std::move( bindings ) ).result();
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
					const SHA256 sha256 { SHA256::fromBuffer( record_sha256 ) };

					auto contained_record_id { co_await db::ensureRecord( sha256, db ) };
					if ( !contained_record_id )
						co_return std::unexpected( MetadataError( 500, std::move( contained_record_id.error() ) ) );

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
					static constexpr char query[] {
#embed "sql/store_archive_entries.sql" suffix(, )
						'\0'
					};

					db::DBBindings map_bindings {};
					map_bindings.bind( archive_id );
					map_bindings.bind( records );
					map_bindings.bind( paths );
					co_await db.query( query, std::move( map_bindings ) ).result();
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
				static constexpr char query[] {
#embed "sql/store_animation_metadata.sql" suffix(, )
					'\0'
				};

				const auto& animation_metadata { std::get< MetadataInfoAnimation >( metadata.m_metadata ) };
				db::DBBindings bindings {};
				bindings.bind( record_id );
				bindings.bind( static_cast< std::int32_t >( animation_metadata.width ) );
				bindings.bind( static_cast< std::int32_t >( animation_metadata.height ) );
				bindings.bind( static_cast< std::int32_t >( animation_metadata.frame_count ) );
				bindings.bind( animation_metadata.duration_s );
				bindings.bind( animation_metadata.loops );
				co_await db.query( query, std::move( bindings ) ).result();
				break;
			}
		case SimpleMimeType::AUDIO:
			{
				static constexpr char query[] {
#embed "sql/store_audio_metadata.sql" suffix(, )
					'\0'
				};

				const auto& audio_metadata { std::get< MetadataInfoAudio >( metadata.m_metadata ) };
				db::DBBindings bindings {};
				bindings.bind( record_id );
				bindings.bind( audio_metadata.duration_s );
				bindings.bind( static_cast< std::int32_t >( audio_metadata.bitrate_bps ) );
				bindings.bind( static_cast< SmallInt >( audio_metadata.channels ) );
				bindings.bind( static_cast< std::int32_t >( audio_metadata.sample_rate ) );
				co_await db.query( query, std::move( bindings ) ).result();
				break;
			}
		case SimpleMimeType::NONE:
			break;
	}

	co_return {};
}

runtime::Task< std::expected< void, MetadataError > > parseAndStoreRecordMetadata(
	db::DBContext& db,
	const RecordID record_id,
	const MimeID mime_id,
	const filesystem::ClusterAccess& cluster,
	std::filesystem::path path )
{
	const auto parsers { modules::ModuleLoader::instance().getParserFor( mime_id ) };
	if ( parsers.empty() )
		co_return std::unexpected( MetadataError( 400, std::format( "No parser found for mime id {}", mime_id ) ) );

	const auto& parser { parsers.front() };

	auto input_e { modules::CallInput::forPath( path ) };
	if ( !input_e )
		co_return std::unexpected( MetadataError(
			500, std::format( "Failed to open the file for record {}: {}", record_id, input_e.error() ) ) );

	const auto input { std::make_shared< const modules::CallInput >( std::move( *input_e ) ) };

	const modules::RemoteCallData call_data { .input = input, .mime_id = mime_id, .extra = {}, .depth = 0 };
	const auto metadata_e { co_await parser->parseFile( call_data ) };

	if ( !metadata_e )
		co_return std::unexpected( MetadataError(
			500, std::format( "Module failed to parse data for record {}: {}", record_id, metadata_e.error() ) ) );

	const auto valid_after_parse { cluster.verify() };
	if ( !valid_after_parse ) co_return std::unexpected( MetadataError( 503, valid_after_parse.error() ) );

	// Parsing runs outside the transaction; all writes for its result share one connection and commit.
	auto transaction { co_await db.transaction() };
	auto written { co_await writeRecordMetadata( transaction, record_id, *metadata_e ) };
	if ( !written )
	{
		co_await transaction.rollback();
		co_return std::unexpected( std::move( written.error() ) );
	}

	const auto valid_before_commit { cluster.verify() };
	if ( !valid_before_commit )
	{
		co_await transaction.rollback();
		co_return std::unexpected( MetadataError( 503, valid_before_commit.error() ) );
	}

	co_await updateRecordFileTimesForScan( transaction, record_id, path );
	co_await transaction.commit();

	co_return {};
}
} // namespace idhan::metadata
