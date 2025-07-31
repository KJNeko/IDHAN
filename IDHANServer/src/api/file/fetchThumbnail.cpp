//
// Created by kj16609 on 6/11/25.
//

#include <fstream>

#include "api/RecordAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"
#include "crypto/SHA256.hpp"
#include "drogon/HttpAppFramework.h"
#include "drogon/utils/coroutine.h"
#include "filesystem/IOUring.hpp"
#include "logging/ScopedTimer.hpp"
#include "modules/ModuleLoader.hpp"
#include "trantor/utils/ConcurrentTaskQueue.h"

namespace idhan::api
{

drogon::Task< std::expected< std::filesystem::path, drogon::HttpResponsePtr > >
	getThumbnailPath( const RecordID record_id, drogon::orm::DbClientPtr db )
{
	const auto sha256_e { co_await SHA256::fromDB( record_id, db ) };
	if ( !sha256_e.has_value() ) co_return std::unexpected( sha256_e.error() );

	const auto& sha256 { sha256_e.value() };

	//Thumbnail should be located in the `thumbnails/f[0:2]/[0:64].thumbnail (XX taken from the hash
	const auto hex { sha256.hex() };
	const auto file_location { std::filesystem::current_path() / "thumbnails" / std::format( "t{}", hex.substr( 0, 2 ) )
		                       / ( std::format( "{}.thumbnail", hex ) ) };

	co_return file_location;
}

drogon::Task< drogon::HttpResponsePtr > RecordAPI::fetchThumbnail( drogon::HttpRequestPtr request, RecordID record_id )
{
	auto db { drogon::app().getDbClient() };

	const auto record_info { co_await db->execSqlCoro(
		"SELECT mime.name as mime_name, cluster_id FROM file_info JOIN mime ON mime.mime_id = file_info.mime_id WHERE record_id = $1",
		record_id ) };

	if ( record_info.empty() )
		co_return createBadRequest(
			"Record {} does not exist or does not have any file info associated with it", record_id );

	const bool force_regenerate { request->getOptionalParameter< bool >( "regenerate" ).value_or( false ) };

	const auto mime_name { record_info[ 0 ][ "mime_name" ].as< std::string >() };
	const auto cluster_id { record_info[ 0 ][ "cluster_id" ].as< ClusterID >() };

	const auto thumbnail_location_e { co_await getThumbnailPath( record_id, db ) };

	if ( !thumbnail_location_e.has_value() ) co_return thumbnail_location_e.error();

	if ( !std::filesystem::exists( thumbnail_location_e.value() ) || force_regenerate )
	{
		using namespace std::chrono_literals;
		logging::ScopedTimer thumbnail_timer { "Thumbnail Process", 5s };
		//We must generate the thumbnail
		auto thumbnailers { modules::ModuleLoader::instance().getThumbnailerFor( mime_name ) };

		if ( thumbnailers.size() == 0 )
		{
			co_return createBadRequest( "No thumbnailer for mime type {} provided by modules", mime_name );
		}

		auto& thumbnailer { thumbnailers[ 0 ] };

		const auto record_path { co_await helpers::getRecordPath( record_id, db ) };

		if ( !record_path.has_value() ) co_return record_path.error();

		// FileMappedData data { record_path.value() };
		FileIOUring io_uring { record_path.value() };

		std::size_t height { 256 };
		std::size_t width { 256 };

		const auto file_size { std::filesystem::file_size( record_path.value() ) };
		std::vector< std::byte > data { co_await io_uring.read( 0, file_size ) };

		const auto thumbnail_info {
			thumbnailer->createThumbnail( data.data(), data.size(), width, height, mime_name )
		};

		if ( !thumbnail_info.has_value() )
			co_return createInternalError( "Thumbnailer had an error: {}", thumbnail_info.error() );

		std::filesystem::create_directories( thumbnail_location_e.value().parent_path() );

		// const auto& thumbnail_data = thumbnail_info.value().data;
		auto thumbnail_data {
			std::make_shared< std::vector< std::uint8_t > >( std::move( thumbnail_info.value().data ) )
		};

		const auto thumbnail_location { thumbnail_location_e.value() };

		//TODO: io_uring for saving this data to a file
		auto save_coro { drogon::queueInLoopCoro(
			drogon::app().getLoop(),
			[ thumbnail_data, thumbnail_location ]()
			{
				if ( std::ofstream ofs( thumbnail_location, std::ios::binary ); ofs )
				{
					ofs.write( reinterpret_cast< const char* >( thumbnail_data->data() ), thumbnail_data->size() );
				}
			} ) };

		co_await save_coro;
	}

	auto response { drogon::HttpResponse::newFileResponse(
		thumbnail_location_e.value(), thumbnail_location_e.value().filename(), drogon::ContentType::CT_IMAGE_PNG ) };

	const auto duration { std::chrono::hours( 1 ) };

	helpers::addFileCacheHeader( response, duration );

	co_return response;
}

} // namespace idhan::api
