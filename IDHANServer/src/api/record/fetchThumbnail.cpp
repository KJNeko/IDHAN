//
// Created by kj16609 on 6/11/25.
//

#include <algorithm>
#include <array>
#include <fstream>

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

namespace
{
//! Discrete thumbnail sizes. A closed set bounds cache growth and gives the grid fixed tile heights.
constexpr std::array< std::size_t, 3 > allowed_thumbnail_sizes { 128, 256, 512 };

//! Size-and-format-keyed cache path: thumbnails/t[hash 0:2]/[hash].[size].png
std::filesystem::path thumbnailPath( const std::string& hex, const std::size_t size )
{
	return getThumbnailsPath() / std::format( "t{}", hex.substr( 0, 2 ) )
	     / std::format( "{}.{}.png", hex, size );
}

//! Pre-size cache path (thumbnails/t[hash 0:2]/[hash].thumbnail). Files written before size support
//! were all 256px PNG, so this is only a valid fallback at size 256.
std::filesystem::path legacyThumbnailPath( const std::string& hex )
{
	return getThumbnailsPath() / std::format( "t{}", hex.substr( 0, 2 ) ) / std::format( "{}.thumbnail", hex );
}

//! Cache-Control for a served thumbnail. There is no revalidation: within max-age the browser reuses
//! its copy with no request at all. Invalidation is manual — after changing generation settings the
//! operator regenerates/purges, and clients pick up the change once their cache entry ages out (or
//! they clear it). max-age comes from [thumbnails] max_age (default 7 days).
std::string thumbnailCacheControl()
{
	return std::format( "private, max-age={}", getThumbnailMaxAge().count() );
}
} // namespace

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
	if ( std::ranges::find( allowed_thumbnail_sizes, size ) == allowed_thumbnail_sizes.end() )
		co_return createBadRequest( "Unsupported thumbnail size {}. Allowed sizes: 128, 256, 512", size );

	const auto mime_name { record_info[ 0 ][ "mime_name" ].as< std::string >() };
	[[maybe_unused]] const auto cluster_id { record_info[ 0 ][ "cluster_id" ].as< ClusterID >() };

	const auto sha256_e { co_await SHA256::fromDB( record_id, db ) };
	if ( !sha256_e ) co_return sha256_e.error();
	const auto hex { sha256_e.value().hex() };

	const auto thumbnail_location { thumbnailPath( hex, size ) };

	// Serve a pre-size cache file if one exists and the request is for the old default (256px PNG),
	// so upgrading does not trigger a full regeneration storm.
	if ( !force_regenerate && !std::filesystem::exists( thumbnail_location ) && size == 256 )
	{
		if ( const auto legacy { legacyThumbnailPath( hex ) }; std::filesystem::exists( legacy ) )
		{
			auto response { drogon::HttpResponse::newFileResponse( legacy, "", drogon::ContentType::CT_IMAGE_PNG ) };
			response->addHeader( "Cache-Control", thumbnailCacheControl() );
			co_return response;
		}
	}

	if ( !std::filesystem::exists( thumbnail_location ) || force_regenerate )
	{
		using namespace std::chrono_literals;
		logging::ScopedTimer thumbnail_timer { "Thumbnail Process", 5s };
		// We must generate the thumbnail
		auto thumbnailers { modules::ModuleLoader::instance().getThumbnailerFor( mime_name ) };

		if ( thumbnailers.empty() )
		{
			co_return createBadRequest( "No thumbnailer for mime type {} provided by modules", mime_name );
		}

		auto& thumbnailer { thumbnailers[ 0 ] };

		// FileMappedData data { record_path.value() };
		auto io_uring_e { co_await filesystem::getIOForRecord( record_id, db ) };
		if ( !io_uring_e ) co_return io_uring_e.error();
		auto& io_uring { io_uring_e.value() };

		const std::size_t height { size };
		const std::size_t width { size };

		const auto& [ data, data_size ] = io_uring.mmapReadOnly();

		const idhan::data_view data_view { static_cast< const std::uint8_t* >( data ), data_size };
		ModuleCallData call_data { .file_view = data_view, .mime_name = mime_name, .extra = {} };

		// check if we have any metadata for this
		const auto extra_metadata {
			co_await db->execSqlCoro( "SELECT json FROM metadata WHERE record_id = $1", record_id )
		};
		if ( extra_metadata.size() > 0 )
		{
			call_data.extra = extra_metadata[ 0 ][ 0 ].as< Json::Value >();
		}

		const auto thumbnail_info { thumbnailer->createThumbnailFile( call_data, width, height ) };

		if ( !thumbnail_info ) co_return createInternalError( "Thumbnailer had an error: {}", thumbnail_info.error() );

		if ( thumbnail_info->cache_thumbnail )
		{
			std::filesystem::create_directories( thumbnail_location.parent_path() );
			FileIOUring io_uring_write { thumbnail_location, FileIOUring::ReadWrite };

			log::debug( "Writing thumbnail to {}", thumbnail_location.string() );

			co_await io_uring_write.write( thumbnail_info->data );
		}
		else
		{
			log::debug( "Skipping thumbnail cache due to module returning NOCACHE flag" );
			auto response { drogon::HttpResponse::newHttpResponse(
				drogon::HttpStatusCode::k200OK, drogon::ContentType::CT_IMAGE_PNG ) };

			std::string body {
				reinterpret_cast< const char* >( thumbnail_info->data.data() ), thumbnail_info->data.size()
			};
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
		drogon::HttpResponse::newFileResponse( thumbnail_location, "", drogon::ContentType::CT_IMAGE_PNG )
	};

	response->addHeader( "Cache-Control", thumbnailCacheControl() );

	co_return response;
}

} // namespace idhan::api
