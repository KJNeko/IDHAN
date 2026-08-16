#include "api/APIMaintenance.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "filesystem/filesystem.hpp"
#include "logging/log.hpp"
#include "paths.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > APIMaintenance::purgeThumbnails(
	[[maybe_unused]] drogon::HttpRequestPtr request )
{
	log::info( "Starting thumbnail purge from: {}", getThumbnailsPath().string() );

	const auto deleted { filesystem::clearThumbnailCache() };

	if ( !deleted )
	{
		log::error( "Error purging thumbnails: {}", deleted.error() );
		co_return createInternalError( "Failed to purge thumbnails: {}", deleted.error() );
	}

	log::info( "Thumbnail purge complete. Deleted: {}", *deleted );

	Json::Value response;
	response[ "success" ] = true;
	response[ "deleted_count" ] = static_cast< Json::UInt64 >( *deleted );
	response[ "message" ] = "Thumbnails purged successfully";

	co_return drogon::HttpResponse::newHttpJsonResponse( response );
}

} // namespace idhan::api
