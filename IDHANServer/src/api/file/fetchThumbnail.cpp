//
// Created by kj16609 on 6/11/25.
//

#include <fstream>
#include <qimage.h>

#include "api/IDHANRecordAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"
#include "crypto/SHA256.hpp"
#include "drogon/utils/coroutine.h"
#include "logging/log.hpp"
#include "metadata/FileMappedData.hpp"
#include "modules/ModuleLoader.hpp"

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

drogon::Task< drogon::HttpResponsePtr > IDHANRecordAPI::
	fetchThumbnail( drogon::HttpRequestPtr request, RecordID record_id )
{
	auto db { drogon::app().getDbClient() };

	const auto record_info { co_await db->execSqlCoro(
		"SELECT mime.name as mime_name, cluster_id FROM file_info JOIN mime ON mime.mime_id = file_info.mime_id WHERE record_id = $1",
		record_id ) };

	if ( record_info.empty() )
		co_return createBadRequest(
			"Record {} does not exist or does not have any file info associated with it", record_id );

	const auto mime_name { record_info[ 0 ][ "mime_name" ].as< std::string >() };
	const auto cluster_id { record_info[ 0 ][ "cluster_id" ].as< ClusterID >() };

	const auto thumbnail_location_e { co_await getThumbnailPath( record_id, db ) };

	if ( !thumbnail_location_e.has_value() ) co_return thumbnail_location_e.error();

	if ( !std::filesystem::exists( thumbnail_location_e.value() ) )
	{
		//We must generate the thumbnail
		auto thumbnailers { modules::ModuleLoader::instance().getThumbnailerFor( mime_name ) };

		if ( thumbnailers.size() == 0 )
		{
			co_return createBadRequest( "No thumbnailer for mime type {} given", mime_name );
		}

		auto& thumbnailer { thumbnailers[ 0 ] };

		const auto record_path { co_await helpers::getRecordPath( record_id, db ) };

		if ( !record_path.has_value() ) co_return record_path.error();

		FileMappedData data { record_path.value() };

		std::size_t height { 256 };
		std::size_t width { 256 };

		const auto thumbnail_info {
			thumbnailer->createThumbnail( data.data(), data.length(), width, height, mime_name )
		};
		if ( !thumbnail_info.has_value() )
			co_return createInternalError( "Thumbnailer had an error: {}", thumbnail_info.error() );

		std::filesystem::create_directories( thumbnail_location_e.value().parent_path() );

		const auto& thumbnail_data = thumbnail_info.value().data;

		if ( std::ofstream ofs( thumbnail_location_e.value(), std::ios::binary ); ofs )
		{
			ofs.write( reinterpret_cast< const char* >( thumbnail_data.data() ), thumbnail_data.size() );
		}
	}

	co_return drogon::HttpResponse::
		newFileResponse( thumbnail_location_e.value(), "", drogon::ContentType::CT_IMAGE_PNG );
}

} // namespace idhan::api
