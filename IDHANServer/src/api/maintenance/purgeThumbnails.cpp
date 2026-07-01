//
// Created by kj16609 on 11/14/25.
//

#include <filesystem>

#include "api/APIMaintenance.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "logging/log.hpp"
#include "paths.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > APIMaintenance::purgeThumbnails(
	[[maybe_unused]] drogon::HttpRequestPtr request )
{
	try
	{
		const auto thumbnails_path { getThumbnailsPath() };

		if ( !std::filesystem::exists( thumbnails_path ) )
		{
			log::warn( "Thumbnails directory does not exist: {}", thumbnails_path.string() );

			Json::Value response;
			response[ "success" ] = true;
			response[ "message" ] = "Thumbnails directory does not exist";
			response[ "deleted_count" ] = 0;

			co_return drogon::HttpResponse::newHttpJsonResponse( response );
		}

		log::info( "Starting thumbnail purge from: {}", thumbnails_path.string() );

		// Count files before removing so we can report a total
		std::size_t deleted_count { 0 };
		for ( const auto& entry : std::filesystem::recursive_directory_iterator( thumbnails_path ) )
		{
			if ( entry.is_regular_file() ) ++deleted_count;
		}

		// Remove all contents and recreate the empty directory
		std::filesystem::remove_all( thumbnails_path );
		std::filesystem::create_directories( thumbnails_path );

		log::info( "Thumbnail purge complete. Deleted: {}", deleted_count );

		Json::Value response;
		response[ "success" ] = true;
		response[ "deleted_count" ] = static_cast< Json::UInt64 >( deleted_count );
		response[ "message" ] = "Thumbnails purged successfully";

		co_return drogon::HttpResponse::newHttpJsonResponse( response );
	}
	catch ( const std::exception& e )
	{
		log::error( "Error purging thumbnails: {}", e.what() );
		co_return createInternalError( "Failed to purge thumbnails: {}", e.what() );
	}
}

} // namespace idhan::api
