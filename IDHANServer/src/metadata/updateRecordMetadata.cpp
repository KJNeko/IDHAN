#include <drogon/drogon.h>
#include <json/json.h>

#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"
#include "filesystem/clusters/ClusterManager.hpp"
#include "filesystem/io/IOUring.hpp"
#include "metadata.hpp"
#include "modules/ModuleLoader.hpp"
#include "records/records.hpp"
#include "threading/ExpectedTask.hpp"

namespace idhan::metadata
{

ExpectedTask< void > updateRecordMetadata( const RecordID record_id, DbClientPtr db, MetadataInfo metadata )
{
	const auto simple_type { metadata.m_simple_type };

	co_await db->execSqlCoro(
		"INSERT INTO metadata (record_id, simple_mime_type) VALUES ($1, $2) "
		"ON CONFLICT (record_id) DO UPDATE SET simple_mime_type = $2",
		record_id,
		std::to_underlying( simple_type ) );

	const Json::Value& json { metadata.m_extra };
	co_await db->execSqlCoro( "UPDATE metadata SET json = $2 WHERE record_id = $1", record_id, json.toStyledString() );

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
				co_await db->execSqlCoro(
					"INSERT INTO image_metadata (record_id, width, height, channels) VALUES ($1, $2, $3, $4) "
					"ON CONFLICT (record_id) DO UPDATE SET width = $2, height = $3, channels = $4",
					record_id,
					image_metadata.width,
					image_metadata.height,
					static_cast< std::uint16_t >( image_metadata.channels ) );

				break;
			}
		case SimpleMimeType::VIDEO:
			{
				const auto& video_metadata { std::get< MetadataInfoVideo >( metadata.m_metadata ) };
				co_await db->execSqlCoro(
					"INSERT INTO video_metadata (record_id, width, height, bitrate, duration, framerate, has_audio) VALUES ($1, $2, $3, $4, $5, $6, $7) "
					"ON CONFLICT (record_id) DO UPDATE SET width = $2, height = $3, bitrate = $4, duration = $5, framerate = $6, has_audio = $7",
					record_id,
					video_metadata.m_width,
					video_metadata.m_height,
					video_metadata.m_bitrate,
					video_metadata.m_duration,
					video_metadata.m_fps,
					video_metadata.m_has_audio );

				break;
			}
		case SimpleMimeType::ARCHIVE:
			{
				const auto& archive_metadata { std::get< MetadataInfoArchive >( metadata.m_metadata ) };

				// map the archive hashes to real records
				std::vector< RecordID > records {};
				for ( const auto& record_sha256 : archive_metadata.contained_hashes )
				{
					const SHA256 sha256 { SHA256::fromBuffer( record_sha256 ) };

					const auto contained_record_id { co_await helpers::createRecord( sha256, db ) };
					return_unexpected_error( contained_record_id );

					records.emplace_back( *contained_record_id );
				}

				// check if there is already metadata for an archive
				const auto existing_metadata { co_await db->execSqlCoro(
					"SELECT archive_id FROM archive_metadata WHERE record_id = $1", record_id ) };

				std::uint32_t archive_id { 0 };

				if ( existing_metadata.size() > 0 )
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

				for ( const auto stored_record_id : records )
				{
					co_await db->execSqlCoro(
						"INSERT INTO archive_map (archive_id, record_id) VALUES ($1, $2) ON CONFLICT DO NOTHING",
						archive_id,
						stored_record_id );
				}

				co_await db->execSqlCoro(
					"INSERT INTO archive_metadata (record_id, archive_id, encrypted) VALUES ($1, $2, $3) ON CONFLICT (record_id) DO UPDATE SET encrypted = $3",
					record_id,
					archive_id,
					archive_metadata.encrypted );

				break;
			}
		case SimpleMimeType::ANIMATION:
			FGL_UNIMPLEMENTED();
			break;
		case SimpleMimeType::AUDIO:
			FGL_UNIMPLEMENTED();
			break;
		case SimpleMimeType::NONE:
			break;
		default:
			FGL_UNIMPLEMENTED();
	}

	co_return {};
}

} // namespace idhan::metadata