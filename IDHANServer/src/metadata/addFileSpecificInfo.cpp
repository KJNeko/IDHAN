//
// Created by kj16609 on 11/13/25.
//

#include "api/helpers/createBadRequest.hpp"
#include "metadata.hpp"

namespace idhan::metadata
{

ExpectedTask< void > addImageInfo( Json::Value& root, const RecordID record_id, DbClientPtr db )
{
	const auto metadata { co_await db->execSqlCoro( "SELECT * FROM image_metadata WHERE record_id = $1", record_id ) };

	if ( metadata.empty() )
		co_return std::unexpected( createInternalError( "Could not find image metadata for record {}", record_id ) );

	root[ "width" ] = metadata[ 0 ][ "width" ].as< std::uint32_t >();
	root[ "height" ] = metadata[ 0 ][ "height" ].as< std::uint32_t >();
	root[ "channels" ] = metadata[ 0 ][ "channels" ].as< std::uint32_t >();

	co_return std::expected< void, drogon::HttpResponsePtr >();
}

ExpectedTask< void > addVideoInfo( Json::Value& root, const RecordID record_id, DbClientPtr db )
{
	const auto metadata { co_await db->execSqlCoro( "SELECT * FROM video_metadata WHERE record_id = $1", record_id ) };

	if ( metadata.empty() )
		co_return std::unexpected( createInternalError( "Could not find video metadata for record {}", record_id ) );

	root[ "width" ] = metadata[ 0 ][ "width" ].as< std::uint32_t >();
	root[ "height" ] = metadata[ 0 ][ "height" ].as< std::uint32_t >();

	root[ "duration" ] = metadata[ 0 ][ "duration" ].as< double >();

	root[ "bitrate" ] = metadata[ 0 ][ "bitrate" ].as< std::uint32_t >();

	root[ "has_audio" ] = metadata[ 0 ][ "has_audio" ].as< bool >();

	co_return {};
}

ExpectedTask< void > addImageProjectInfo( Json::Value& root, const RecordID record_id, DbClientPtr db )
{
	const auto metadata {
		co_await db->execSqlCoro( "SELECT * FROM image_project_metadata WHERE record_id = $1", record_id )
	};

	if ( metadata.empty() )
		co_return std::unexpected(
			createInternalError( "Could not find image project metadata for record {}", record_id ) );

	root[ "width" ] = metadata[ 0 ][ "width" ].as< std::uint32_t >();
	root[ "height" ] = metadata[ 0 ][ "height" ].as< std::uint32_t >();
	root[ "channels" ] = metadata[ 0 ][ "channels" ].as< SmallInt >();
	root[ "layers" ] = metadata[ 0 ][ "layers" ].as< SmallInt >();

	co_return {};
}

ExpectedTask< void > addFileSpecificInfo( Json::Value& root, const RecordID record_id, DbClientPtr db )
{
	auto simple_mime_result {
		co_await db->execSqlCoro( "SELECT simple_mime_type FROM metadata WHERE record_id = $1", record_id )
	};

	if ( simple_mime_result.empty() ) // Could not find any mime info for this record, Try parsing for it.
	{
		const auto parsed_metadata { co_await tryParseRecordMetadata( record_id, db ) };

		if ( !parsed_metadata ) co_return std::unexpected( parsed_metadata.error() );

		simple_mime_result =
			co_await db->execSqlCoro( "SELECT simple_mime_type FROM metadata WHERE record_id = $1", record_id );
	}

	if ( simple_mime_result.empty() )
		co_return std::unexpected( createInternalError( "Failed to get simple mime type for record {}", record_id ) );

	const SimpleMimeType simple_mime_type { simple_mime_result[ 0 ][ "simple_mime_type" ].as< std::uint16_t >() };

	switch ( simple_mime_type )
	{
		case SimpleMimeType::IMAGE:
			{
				const auto result { co_await addImageInfo( root, record_id, db ) };
				if ( !result ) co_return std::unexpected( result.error() );
				break;
			}
		case SimpleMimeType::VIDEO:
			{
				const auto result { co_await addVideoInfo( root, record_id, db ) };
				if ( !result ) co_return std::unexpected( result.error() );
				break;
			}
		case SimpleMimeType::IMAGE_PROJECT:
			{
				const auto result { co_await addImageProjectInfo( root, record_id, db ) };
				if ( !result ) co_return std::unexpected( result.error() );
				break;
			}
		case SimpleMimeType::ANIMATION:
			[[fallthrough]];
		case SimpleMimeType::AUDIO:
			[[fallthrough]];
		case SimpleMimeType::NONE:
			[[fallthrough]];
		default:
			co_return std::unexpected( createInternalError(
				"No handler for adding metadata with given simple mime {}", static_cast< int >( simple_mime_type ) ) );
	}

	co_return std::expected< void, drogon::HttpResponsePtr >();
}

} // namespace idhan::metadata