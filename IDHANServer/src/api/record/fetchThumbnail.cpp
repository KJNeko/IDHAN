#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
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
#include "metadata/metadata.hpp"
#include "modules/ModuleLoader.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsuggest-override"
#include "filesystem/filesystem.hpp"
#include "paths.hpp"
#include "trantor/utils/ConcurrentTaskQueue.h"
#pragma GCC diagnostic pop

namespace idhan::api
{

std::filesystem::path thumbnailPath( const std::string& hex, const std::size_t size )
{
	return getThumbnailsPath() / std::format( "t{}", hex.substr( 0, 2 ) ) / std::format( "{}.{}.webp", hex, size );
}

//! Invalidation is manual: after changing generation settings the operator regenerates or purges.
std::string thumbnailCacheControl()
{
	return std::format( "private, max-age={}", helpers::default_max_age.count() );
}

//! Zero-byte cache files count as misses. path is by value because the kernel reads it after suspension.
drogon::Task< bool > hasCachedThumbnail( std::filesystem::path path )
{
	const auto size { co_await IOUring::getInstance().fileSize( path ) };

	co_return size.has_value() && size.value() > 0;
}

drogon::Task< drogon::HttpResponsePtr > RecordAPI::fetchThumbnail( drogon::HttpRequestPtr request, RecordID record_id )
{
	auto db { drogon::app().getDbClient() };

	const auto record_info { co_await db->execSqlCoro(
		"SELECT file_info.mime_id as mime_id, cluster_id FROM file_info JOIN "
		"mime ON mime.mime_id = file_info.mime_id WHERE record_id = $1",
		record_id ) };

	if ( record_info.empty() )
		co_return createNotFound(
			"Record {} does not exist or does not have any file info associated with it", record_id );

	const bool force_regenerate { request->getOptionalParameter< bool >( "regenerate" ).value_or( false ) };

	const std::size_t size { request->getOptionalParameter< std::size_t >( "size" ).value_or( 256 ) };

	const auto mime_id { record_info[ 0 ][ "mime_id" ].as< MimeID >() };
	[[maybe_unused]] const auto cluster_id { record_info[ 0 ][ "cluster_id" ].as< ClusterID >() };

	const auto sha256_e { co_await SHA256::fromDB( record_id, db ) };
	if ( !sha256_e ) co_return sha256_e.error();
	const auto hex { sha256_e.value().hex() };

	const auto thumbnail_location { thumbnailPath( hex, size ) };

	const bool cache_enabled { getThumbnailCachingEnabled() };

	// Ordered so the stat is skipped entirely when the answer is already known.
	if ( !cache_enabled || force_regenerate || !co_await hasCachedThumbnail( thumbnail_location ) )
	{
		auto thumbnailers { modules::ModuleLoader::instance().getThumbnailerFor( mime_id ) };

		if ( thumbnailers.empty() )
		{
			co_return createBadRequest( "No thumbnailer for mime id {} provided by modules", mime_id );
		}

		auto& thumbnailer { thumbnailers[ 0 ] };

		auto input_e { co_await filesystem::openRecordInput( record_id, db ) };
		if ( !input_e ) co_return input_e.error();

		const std::size_t height { size };
		const std::size_t width { size };

		modules::RemoteCallData call_data { .input = *input_e, .mime_id = mime_id };

		const auto extra_metadata {
			co_await db->execSqlCoro( "SELECT simple_mime_type, json FROM metadata WHERE record_id = $1", record_id )
		};

		if ( !extra_metadata.empty() )
		{
			call_data.extra = extra_metadata[ 0 ][ "json" ].as< Json::Value >();

			const auto simple_type {
				static_cast< SimpleMimeType >( extra_metadata[ 0 ][ "simple_mime_type" ].as< std::uint16_t >() )
			};

			if ( simple_type == SimpleMimeType::ARCHIVE )
				co_await metadata::applyArchiveEntries( call_data.extra, record_id, db );
		}

		//TODO: If not caching, Do not use the file endpoint
		const auto thumbnail_info { co_await thumbnailer->createThumbnailFile( call_data, width, height ) };

		if ( !thumbnail_info ) co_return createInternalError( "Thumbnailer had an error: {}", thumbnail_info.error() );

		// Cache to disk only for sizes the operator has opted in; other sizes are still generated and
		// served, just never written (keeps the cache from exploding across arbitrary requested sizes).
		const bool size_is_cacheable { std::ranges::contains( getCacheableThumbnailSizes(), size ) };
		const bool should_cache { cache_enabled && thumbnail_info->cache_thumbnail && size_is_cacheable };

		if ( should_cache )
		{
			auto& io { IOUring::getInstance() };

			if ( const auto mkdir_result { co_await io.createDirectories( thumbnail_location.parent_path() ) };
			     mkdir_result != 0 )
				co_return createInternalError(
					"Failed to create thumbnail cache directory {}: {}",
					thumbnail_location.parent_path().string(),
					std::strerror( -mkdir_result ) );

			static std::atomic< std::uint64_t > temp_counter { 0 };
			const auto temp_location {
				thumbnail_location.parent_path() / std::format( "{}.{}.{}.tmp", hex, size, temp_counter.fetch_add( 1 ) )
			};

			log::debug( "Writing thumbnail to {} via {}", thumbnail_location.string(), temp_location.string() );

			const auto& pixel_data { thumbnail_info->m_pixel_data };

			std::string write_error {};

			try
			{
				FileIOUring io_uring_write { temp_location, FileIOUring::ReadWrite };
				co_await io_uring_write.write( pixel_data );
			}
			catch ( const std::exception& e )
			{
				write_error = e.what();
			}

			if ( write_error.empty() )
			{
				const auto written_size { co_await io.fileSize( temp_location ) };

				if ( !written_size )
					write_error =
						std::format( "could not stat the written file: {}", std::strerror( -written_size.error() ) );
				else if ( written_size.value() != pixel_data.size() )
					write_error = std::format(
						"wrote {} bytes but the file is {} bytes", pixel_data.size(), written_size.value() );
			}

			if ( write_error.empty() )
			{
				if ( const auto rename_result { co_await io.renameFile( temp_location, thumbnail_location ) };
				     rename_result != 0 )
					write_error = std::format( "could not publish the thumbnail: {}", std::strerror( -rename_result ) );
			}

			if ( !write_error.empty() )
			{
				co_await io.removeFile( temp_location );

				co_return createInternalError( "Failed to write thumbnail for record {}: {}", record_id, write_error );
			}
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

	if ( !co_await hasCachedThumbnail( thumbnail_location ) )
	{
		co_await IOUring::getInstance().removeFile( thumbnail_location );

		co_return createInternalError(
			"Thumbnail {} is missing or empty after a successful write. See previous warnings/errors",
			thumbnail_location.string() );
	}

	auto response {
		drogon::HttpResponse::newFileResponse( thumbnail_location, "", drogon::ContentType::CT_IMAGE_WEBP )
	};

	response->addHeader( "Cache-Control", thumbnailCacheControl() );

	co_return response;
}

} // namespace idhan::api
