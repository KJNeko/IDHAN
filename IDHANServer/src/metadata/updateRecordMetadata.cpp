#include <drogon/drogon.h>
#include <json/json.h>

#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"
#include "db/drogonArrayBind.hpp"
#include "filesystem/clusters/ClusterManager.hpp"
#include "filesystem/io/IOUring.hpp"
#include "metadata.hpp"
#include "modules/ModuleLoader.hpp"
#include "records/records.hpp"
#include "threading/ExpectedTask.hpp"

namespace idhan::metadata
{

static std::string phashToBitLiteral( const PerceptualHash& phash )
{
	constexpr char HEX[] { "0123456789abcdef" };
	std::string literal( 1 + ( phash.size() * 2 ), 'x' );
	for ( std::size_t i = 0; i < phash.size(); ++i )
	{
		const auto byte { static_cast< std::uint8_t >( phash[ i ] ) };
		literal[ 1 + ( i * 2 ) ] = HEX[ byte >> 4 ];
		literal[ 2 + ( i * 2 ) ] = HEX[ byte & 0x0f ];
	}
	return literal;
}

ExpectedTask< void > updateRecordMetadata( const RecordID record_id, DbClientPtr db, MetadataInfo metadata )
{
	const auto simple_type { metadata.m_simple_type };

	if ( metadata.m_extra.isNull() )
	{
		co_await db->execSqlCoro(
			"INSERT INTO metadata (record_id, simple_mime_type) VALUES ($1, $2) "
			"ON CONFLICT (record_id) DO UPDATE SET simple_mime_type = $2",
			record_id,
			std::to_underlying( simple_type ) );
	}
	else
	{
		co_await db->execSqlCoro(
			"INSERT INTO metadata (record_id, simple_mime_type, json) VALUES ($1, $2, $3) "
			"ON CONFLICT (record_id) DO UPDATE SET simple_mime_type = $2, json = $3",
			record_id,
			std::to_underlying( simple_type ),
			metadata.m_extra.toStyledString() );
	}

	switch ( simple_type )
	{
		case SimpleMimeType::IMAGE_PROJECT:
			{
				const auto& project_metadata { std::get< MetadataInfoImageProject >( metadata.m_metadata ) };
				co_await db->execSqlCoro(
					"INSERT INTO image_project_metadata (record_id, width, height, channels, layers) VALUES ($1, $2, $3, $4, $5) "
					"ON CONFLICT (record_id) DO UPDATE SET width = $2, height = $3, channels = $4, layers = $5",
					record_id,
					project_metadata.image_info.width,
					project_metadata.image_info.height,
					static_cast< SmallInt >( project_metadata.image_info.channels ),
					static_cast< SmallInt >( project_metadata.layers ) );

				break;
			}
		case SimpleMimeType::IMAGE_TYPE:
			{
				const auto& image_metadata { std::get< MetadataInfoImage >( metadata.m_metadata ) };
				std::optional< std::string > phash {};
				if ( image_metadata.phash ) phash = phashToBitLiteral( *image_metadata.phash );

				const auto& embedded { image_metadata.embedded };

				co_await db->execSqlCoro(
					"INSERT INTO image_metadata (record_id, width, height, channels, phash, has_exif, has_gps, has_xmp, has_iptc, has_icc_profile) "
					"VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10) "
					"ON CONFLICT (record_id) DO UPDATE SET width = $2, height = $3, channels = $4, phash = $5, "
					"has_exif = $6, has_gps = $7, has_xmp = $8, has_iptc = $9, has_icc_profile = $10",
					record_id,
					image_metadata.width,
					image_metadata.height,
					static_cast< std::uint16_t >( image_metadata.channels ),
					std::move( phash ),
					embedded.exif,
					embedded.gps,
					embedded.xmp,
					embedded.iptc,
					embedded.icc_profile );

				break;
			}
		case SimpleMimeType::VIDEO:
			{
				const auto& video_metadata { std::get< MetadataInfoVideo >( metadata.m_metadata ) };
				co_await db->execSqlCoro(
					"INSERT INTO video_metadata (record_id, width, height, bitrate, duration, framerate, has_audio) VALUES ($1, $2, $3, $4, $5, $6, $7) "
					"ON CONFLICT (record_id) DO UPDATE SET width = $2, height = $3, bitrate = $4, duration = $5, framerate = $6, has_audio = $7",
					record_id,
					video_metadata.width,
					video_metadata.height,
					video_metadata.bitrate_bps,
					video_metadata.duration_s,
					video_metadata.fps,
					video_metadata.has_audio );

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

					const auto contained_record_id { co_await helpers::createRecord( sha256, db ) };
					return_unexpected_error( contained_record_id );

					records.emplace_back( *contained_record_id );
					paths.emplace_back( path );
				}

				const auto existing_metadata { co_await db->execSqlCoro(
					"SELECT archive_id FROM archive_metadata WHERE record_id = $1", record_id ) };

				std::uint32_t archive_id { 0 };

				if ( !existing_metadata.empty() )
				{
					archive_id = existing_metadata[ 0 ][ 0 ].as< std::uint32_t >();
				}

				if ( archive_id == 0 )
				{
					const auto inserted_archive_id {
						co_await db->execSqlCoro( "INSERT INTO archives DEFAULT VALUES RETURNING archive_id" )
					};

					archive_id = inserted_archive_id[ 0 ][ 0 ].as< std::uint32_t >();
				}

				// The parse is authoritative for the whole archive, so entries it no longer lists go.
				co_await db->execSqlCoro(
					"DELETE FROM archive_map WHERE archive_id = $1 AND path <> ALL($2::text[])",
					archive_id,
					std::vector< std::string > { paths } );

				if ( !records.empty() )
				{
					co_await db->execSqlCoro(
						"INSERT INTO archive_map (archive_id, record_id, path) "
						"SELECT $1, entry.record_id, entry.path "
						"FROM UNNEST($2::" RECORD_PG_TYPE_NAME "[], $3::text[]) AS entry(record_id, path) "
						"ON CONFLICT (archive_id, path) DO UPDATE SET record_id = EXCLUDED.record_id",
						archive_id,
						std::move( records ),
						std::move( paths ) );
				}

				co_await db->execSqlCoro(
					"INSERT INTO archive_metadata (record_id, archive_id, encrypted) VALUES ($1, $2, $3) ON CONFLICT (record_id) DO UPDATE SET encrypted = $3",
					record_id,
					archive_id,
					archive_metadata.encrypted );

				break;
			}
		case SimpleMimeType::ANIMATION:
			{
				const auto& animation_metadata { std::get< MetadataInfoAnimation >( metadata.m_metadata ) };
				co_await db->execSqlCoro(
					"INSERT INTO animation_metadata (record_id, width, height, frame_count, duration, loops) VALUES ($1, $2, $3, $4, $5, $6) "
					"ON CONFLICT (record_id) DO UPDATE SET width = $2, height = $3, frame_count = $4, duration = $5, loops = $6",
					record_id,
					animation_metadata.width,
					animation_metadata.height,
					animation_metadata.frame_count,
					animation_metadata.duration_s,
					animation_metadata.loops );

				break;
			}
		case SimpleMimeType::AUDIO:
			{
				const auto& audio_metadata { std::get< MetadataInfoAudio >( metadata.m_metadata ) };
				co_await db->execSqlCoro(
					"INSERT INTO audio_metadata (record_id, duration, bitrate, channels, sample_rate) VALUES ($1, $2, $3, $4, $5) "
					"ON CONFLICT (record_id) DO UPDATE SET duration = $2, bitrate = $3, channels = $4, sample_rate = $5",
					record_id,
					audio_metadata.duration_s,
					audio_metadata.bitrate_bps,
					static_cast< SmallInt >( audio_metadata.channels ),
					audio_metadata.sample_rate );

				break;
			}
		case SimpleMimeType::NONE:
			break;
		default:
			FGL_UNIMPLEMENTED();
	}

	co_return {};
}

} // namespace idhan::metadata
