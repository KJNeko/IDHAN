//
// Created by kj16609 on 11/13/25.
//
#include <drogon/drogon.h>
#include <json/json.h>

#include "../filesystem/clusters/ClusterManager.hpp"
#include "../filesystem/io/IOUring.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"
#include "metadata.hpp"
#include "modules/ModuleLoader.hpp"
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
		simple_type );

	Json::Value json {};
	Json::Reader reader {};
	if ( !metadata.m_extra.empty() )
	{
		if ( !reader.parse( metadata.m_extra, json ) )
			co_return std::unexpected( createBadRequest( "Failed to parse metadata \"{}\"", metadata.m_extra ) );

		co_await db->execSqlCoro(
			"UPDATE metadata SET json = $2 WHERE record_id = $1", record_id, json.toStyledString() );
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
		case SimpleMimeType::IMAGE:
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