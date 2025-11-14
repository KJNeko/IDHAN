//
// Created by kj16609 on 11/14/25.
//

#include "../APIMaintenance.hpp"

#include "../../paths.hpp"
#include "../../logging/log.hpp"

#include <filesystem>

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > APIMaintenance::purgeThumbnails( drogon::HttpRequestPtr request )
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

			auto resp { drogon::HttpResponse::newHttpJsonResponse( response ) };
			resp->setStatusCode( drogon::k200OK );
			co_return resp;
		}

		std::size_t deleted_count { 0 };
		std::size_t failed_count { 0 };

		log::info( "Starting thumbnail purge from: {}", thumbnails_path.string() );

		// Iterate through all files in the thumbnails directory
		for ( const auto& entry : std::filesystem::recursive_directory_iterator( thumbnails_path ) )
		{
			if ( entry.is_regular_file() )
			{
				try
				{
					std::filesystem::remove( entry.path() );
					++deleted_count;
					log::trace( "Deleted thumbnail: {}", entry.path().string() );
				}
				catch ( const std::filesystem::filesystem_error& e )
				{
					++failed_count;
					log::error( "Failed to delete thumbnail {}: {}", entry.path().string(), e.what() );
				}
			}
		}

		// Clean up empty directories
		try
		{
			for ( const auto& entry : std::filesystem::recursive_directory_iterator( thumbnails_path ) )
			{
				if ( entry.is_directory() && std::filesystem::is_empty( entry.path() ) )
				{
					std::filesystem::remove( entry.path() );
					log::trace( "Removed empty directory: {}", entry.path().string() );
				}
			}
		}
		catch ( const std::filesystem::filesystem_error& e )
		{
			log::warn( "Error cleaning up empty directories: {}", e.what() );
		}

		log::info( "Thumbnail purge complete. Deleted: {}, Failed: {}", deleted_count, failed_count );

		Json::Value response;
		response[ "success" ] = true;
		response[ "deleted_count" ] = static_cast< Json::UInt64 >( deleted_count );
		response[ "failed_count" ] = static_cast< Json::UInt64 >( failed_count );
		response[ "message" ] = "Thumbnails purged successfully";

		auto resp { drogon::HttpResponse::newHttpJsonResponse( response ) };
		resp->setStatusCode( drogon::k200OK );
		co_return resp;
	}
	catch ( const std::exception& e )
	{
		log::error( "Error purging thumbnails: {}", e.what() );

		Json::Value response;
		response[ "success" ] = false;
		response[ "error" ] = e.what();

		auto resp { drogon::HttpResponse::newHttpJsonResponse( response ) };
		resp->setStatusCode( drogon::k500InternalServerError );
		co_return resp;
	}
}

} // namespace idhan::api