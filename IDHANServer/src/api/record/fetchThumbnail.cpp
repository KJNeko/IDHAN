#include <algorithm>
#include <array>
#include <fstream>
#include <vector>

#include "api/RecordAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"
#include "crypto/SHA256.hpp"
#include "drogon/HttpAppFramework.h"
#include "drogon/utils/coroutine.h"
#include "filesystem/io/IOUring.hpp"
#include "logging/ScopedTimer.hpp"
#include "modules/ModuleLoader.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsuggest-override"
#include "filesystem/filesystem.hpp"
#include "paths.hpp"
#include "trantor/utils/ConcurrentTaskQueue.h"
#pragma GCC diagnostic pop

namespace idhan::api
{

//! Size-and-format-keyed cache path: thumbnails/t[hash 0:2]/[hash].[size].webp
std::filesystem::path thumbnailPath( const std::string& hex, const std::size_t size )
{
	return getThumbnailsPath() / std::format( "t{}", hex.substr( 0, 2 ) ) / std::format( "{}.{}.webp", hex, size );
}

//! Cache-Control for a served thumbnail. There is no revalidation: within max-age the browser reuses
//! its copy with no request at all. The window is a fixed one year (not immutable, and not operator
//! configurable). Invalidation is manual — after changing generation settings the operator
//! regenerates/purges, and clients pick up the change once their cache entry ages out (or they clear
//! it).
std::string thumbnailCacheControl()
{
	return std::format( "private, max-age={}", helpers::default_max_age.count() );
}

drogon::Task< drogon::HttpResponsePtr > RecordAPI::fetchThumbnail( drogon::HttpRequestPtr request, RecordID record_id )
{
	auto db { drogon::app().getDbClient() };

	const auto record_info { co_await db->execSqlCoro(
		"SELECT mime.name as mime_name, cluster_id FROM file_info JOIN "
		"mime ON mime.mime_id = file_info.mime_id WHERE record_id = $1",
		record_id ) };

	if ( record_info.empty() )
		co_return createNotFound(
			"Record {} does not exist or does not have any file info associated with it", record_id );

	const bool force_regenerate { request->getOptionalParameter< bool >( "regenerate" ).value_or( false ) };

	const std::size_t size { request->getOptionalParameter< std::size_t >( "size" ).value_or( 256 ) };

	const auto mime_name { record_info[ 0 ][ "mime_name" ].as< std::string >() };
	[[maybe_unused]] const auto cluster_id { record_info[ 0 ][ "cluster_id" ].as< ClusterID >() };

	const auto sha256_e { co_await SHA256::fromDB( record_id, db ) };
	if ( !sha256_e ) co_return sha256_e.error();
	const auto hex { sha256_e.value().hex() };

	const auto thumbnail_location { thumbnailPath( hex, size ) };

	const bool cache_enabled { getThumbnailCachingEnabled() };

	if ( !cache_enabled || !std::filesystem::exists( thumbnail_location ) || force_regenerate )
	{
		auto thumbnailers { modules::ModuleLoader::instance().getThumbnailerFor( mime_name ) };

		if ( thumbnailers.empty() )
		{
			co_return createBadRequest( "No thumbnailer for mime type {} provided by modules", mime_name );
		}

		auto& thumbnailer { thumbnailers[ 0 ] };

		auto input_e { co_await filesystem::openRecordInput( record_id, db ) };
		if ( !input_e ) co_return input_e.error();

		const std::size_t height { size };
		const std::size_t width { size };

		modules::RemoteCallData call_data { .input = *input_e, .mime_name = mime_name, .extra = {}, .depth = 0 };

		// check if we have any metadata for this
		const auto extra_metadata {
			co_await db->execSqlCoro( "SELECT json FROM metadata WHERE record_id = $1", record_id )
		};

		if ( !extra_metadata.empty() )
		{
			call_data.extra = extra_metadata[ 0 ][ 0 ].as< Json::Value >();
		}

		const auto thumbnail_info { co_await thumbnailer->createThumbnailFile( call_data, width, height ) };

		if ( !thumbnail_info ) co_return createInternalError( "Thumbnailer had an error: {}", thumbnail_info.error() );

		// Cache to disk only for sizes the operator has opted in; other sizes are still generated and
		// served, just never written (keeps the cache from exploding across arbitrary requested sizes).
		const bool size_is_cacheable { std::ranges::contains( getCacheableThumbnailSizes(), size ) };
		const bool should_cache { cache_enabled && thumbnail_info->cache_thumbnail && size_is_cacheable };

		if ( should_cache )
		{
			std::filesystem::create_directories( thumbnail_location.parent_path() );
			FileIOUring io_uring_write { thumbnail_location, FileIOUring::ReadWrite };

			log::debug( "Writing thumbnail to {}", thumbnail_location.string() );

			co_await io_uring_write.write( thumbnail_info->m_pixel_data );
		}
		else
		{
			if ( !cache_enabled )
				log::debug( "Skipping thumbnail cache: the on-disk cache is disabled" );
			else if ( !thumbnail_info->cache_thumbnail )
				log::debug( "Skipping thumbnail cache due to module returning NOCACHE flag" );
			else
				log::debug( "Skipping thumbnail cache: size {} is not in the cacheable set", size );

			auto response { drogon::HttpResponse::newHttpResponse(
				drogon::HttpStatusCode::k200OK, drogon::ContentType::CT_IMAGE_WEBP ) };

			std::string body { reinterpret_cast< const char* >( thumbnail_info->m_pixel_data.data() ),
				               thumbnail_info->m_pixel_data.size() };
			response->setBody( std::move( body ) );

			// The module opted out of caching this thumbnail, so tell the browser not to store it either.
			response->addHeader( "Cache-Control", "no-store" );

			co_return response;
		}
	}

	if ( !std::filesystem::exists( thumbnail_location ) )
	{
		co_return createInternalError(
			"Thumbnail did not exist for record {}, Writing might have failed. See previous warnings/errors",
			thumbnail_location.string() );
	}

	auto response {
		drogon::HttpResponse::newFileResponse( thumbnail_location, "", drogon::ContentType::CT_IMAGE_WEBP )
	};

	response->addHeader( "Cache-Control", thumbnailCacheControl() );

	co_return response;
}

} // namespace idhan::api
