#include "thumbnail.hpp"

#include <folly/FileUtil.h>
#include <json/reader.h>
#include <sys/stat.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <ranges>
#include <span>
#include <sstream>
#include <unistd.h>
#include <vector>

#include "IDHANTypes.hpp"
#include "apiAuth.hpp"
#include "filesystem/PathHelpers.hpp"
#include "filesystem/clusters/clusterIdentity.hpp"
#include "filesystem/io/IOUring.hpp"
#include "idhan/SHA256.hpp"
#include "idhan/db/DBBindings.hpp"
#include "idhan/db/DBContext.hpp"
#include "idhan/http/response/FileResponse.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"
#include "modules/CallInput.hpp"
#include "modules/ModuleLoader.hpp"
#include "paths.hpp"
#include "recordValidator.hpp"

namespace idhan::httpapi
{

[[nodiscard]] static std::filesystem::path thumbnailPath( const std::string_view hex, const std::size_t size )
{
	return getThumbnailsPath() / std::format( "t{}", hex.substr( 0, 2 ) ) / std::format( "{}.{}.webp", hex, size );
}

[[nodiscard]] static bool isCachedThumbnail( const std::filesystem::path& path )
{
	std::error_code error {};
	if ( !std::filesystem::is_regular_file( path, error ) ) return false;

	const auto size { std::filesystem::file_size( path, error ) };
	return !error && size > 0;
}

[[nodiscard]] static std::expected< Json::Value, std::string > parseExtra( const std::string& document )
{
	Json::CharReaderBuilder builder {};
	Json::Value extra {};
	std::string errors {};
	std::istringstream stream { document };

	if ( !Json::parseFromStream( builder, stream, &extra, &errors ) )
		return std::unexpected( std::format( "stored metadata is not valid JSON: {}", errors ) );

	return extra;
}

[[nodiscard]] static runtime::Task< std::expected< void, std::string > > cacheThumbnail(
	const std::filesystem::path path,
	const std::span< const std::byte > bytes )
{
	auto& uring { IOUring::getInstance() };

	const int created { co_await uring.createDirectories( path.parent_path() ) };
	if ( created < 0 )
		co_return std::unexpected( std::format( "could not create cache directory: {}", std::strerror( -created ) ) );

	static std::atomic< std::uint64_t > temporary_id { 0 };
	const auto temporary {
		path.parent_path() / std::format( ".{}.{}.tmp", path.filename().string(), temporary_id.fetch_add( 1 ) )
	};

	const int descriptor { co_await uring.openFile( temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600 ) };
	if ( descriptor < 0 )
		co_return std::unexpected( std::format( "could not create cache file: {}", std::strerror( -descriptor ) ) );

	FileIOUring file { descriptor, 0, FileIOUring::ReadWrite };
	std::string failure {};
	try
	{
		co_await file.write( bytes );
	}
	catch ( const std::exception& error )
	{
		failure = std::format( "could not write cache file: {}", error.what() );
	}

	if ( failure.empty() )
	{
		const int synced { co_await file.sync() };
		if ( synced < 0 ) failure = std::format( "could not sync cache file: {}", std::strerror( -synced ) );
	}

	const int closed { co_await file.close() };
	if ( failure.empty() && closed < 0 )
		failure = std::format( "could not close cache file: {}", std::strerror( -closed ) );

	if ( !failure.empty() )
	{
		(void)co_await uring.removeFile( temporary );
		co_return std::unexpected( std::move( failure ) );
	}

	const int renamed { co_await uring.renameFile( temporary, path ) };
	if ( renamed < 0 )
	{
		(void)co_await uring.removeFile( temporary );
		co_return std::unexpected( std::format( "could not publish cache file: {}", std::strerror( -renamed ) ) );
	}

	co_return {};
}

static http::HTTPTask< http::Response > serveCachedThumbnail(
	const http::RequestContext& context,
	const std::filesystem::path& path,
	const std::string_view hex )
{
	auto file_e { folly::File::makeFile( path.c_str(), O_RDONLY ) };
	if ( !file_e ) co_return http::Response::error( 500, "Generated thumbnail could not be opened" );

	auto file { std::move( *file_e ) };
	struct stat status {};
	if ( ::fstat( file.fd(), &status ) != 0 || !S_ISREG( status.st_mode ) || status.st_size == 0 )
		co_return http::Response::error( 500, "Generated thumbnail is invalid" );

	http::FileMetadata metadata {
		.size = static_cast< std::uint64_t >( status.st_size ),
		.modified = std::chrono::system_clock::from_time_t( status.st_mtime ),
		.changed = std::chrono::system_clock::from_time_t( status.st_ctime ),
		.etag = std::format( "{:x}-{:x}", status.st_mtime, status.st_size ),
		.content_type = "image/webp",
		.filename = std::format( "{}.webp", hex ),
	};
	co_return co_await http::FileResponse::create( context, std::move( file ), std::move( metadata ) );
}

http::HTTPTask< http::Response > recordThumbnailResponse(
	db::DBContext& db,
	http::RequestContext& context,
	const RecordID record_id,
	const FetchThumbnailQuery& parameters )
{
	const auto size { parameters.size.value_or( 256 ) };
	if ( size == 0 || size > 4096 ) co_return http::Response::error( 400, "size must be between 1 and 4096" );

	db::DBBindings bindings {};
	bindings.bind( record_id );
	const auto rows {
		co_await db
			.query(
				//language=postgresql
				R"SQL(
					SELECT
					    file_info.mime_id,
					    records.sha256,
					    file_info.cluster_id,
					    COALESCE(file_info.extension, mime.best_extension, ''),
					    COALESCE(metadata.json::text, '{}')
					FROM records
					    JOIN file_info USING (record_id)
					    LEFT JOIN mime ON mime.mime_id = file_info.mime_id
					    LEFT JOIN metadata USING (record_id)
					WHERE records.record_id = $1
					    AND file_info.cluster_id IS NOT NULL
				)SQL",
				std::move( bindings ) )
			.rows< MimeID, std::span< const std::byte >, ClusterID, std::string, std::string >()
	};
	if ( rows.empty() ) co_return http::Response::error( 404, "Record has no file information" );

	const auto& [ mime_id, sha256_bytes, cluster_id, extension, metadata_document ] { rows.row( 0 ) };
	const SHA256 sha256 { SHA256::fromBuffer( sha256_bytes ) };
	const auto hex { sha256.hex() };
	const auto path { thumbnailPath( hex, size ) };
	const bool regenerate { parameters.regenerate.value_or( false ) };
	if ( !regenerate && isCachedThumbnail( path ) ) co_return co_await serveCachedThumbnail( context, path, hex );

	const auto thumbnailers { modules::ModuleLoader::instance().getThumbnailerFor( mime_id ) };
	if ( thumbnailers.empty() )
		co_return http::Response::error( 400, "No thumbnailer is available for this file type" );

	auto cluster { co_await filesystem::openCluster( db, cluster_id ) };
	if ( !cluster ) co_return http::Response::error( 503, cluster.error() );

	const auto record_path { cluster->root() / filesystem::getClusterRelativePath( sha256, extension ) };
	auto input_e { modules::CallInput::forPath( record_path ) };
	if ( !input_e )
		co_return http::Response::error( 500, std::format( "Record file could not be opened: {}", input_e.error() ) );

	auto extra_e { parseExtra( metadata_document ) };
	if ( !extra_e ) co_return http::Response::error( 500, extra_e.error() );

	db::DBBindings archive_bindings {};
	archive_bindings.bind( record_id );
	const auto archive_rows {
		co_await db
			.query(
				//language=postgresql
				R"SQL(
					SELECT
					    archive_metadata.encrypted,
					    encode(records.sha256, 'hex'),
					    archive_map.path
					FROM archive_metadata
					    LEFT JOIN archive_map ON archive_map.archive_id = archive_metadata.archive_id
					    LEFT JOIN records ON records.record_id = archive_map.record_id
					WHERE archive_metadata.record_id = $1
				)SQL",
				std::move( archive_bindings ) )
			.rows< bool, std::optional< std::string >, std::optional< std::string > >()
	};
	for ( const auto& [ encrypted, member_hash, member_path ] : archive_rows )
	{
		( *extra_e )[ "encrypted" ] = encrypted;
		if ( member_hash && member_path ) ( *extra_e )[ *member_hash ] = *member_path;
	}

	auto input { std::make_shared< const modules::CallInput >( std::move( *input_e ) ) };
	modules::RemoteCallData call { .input = std::move( input ), .mime_id = mime_id, .extra = std::move( *extra_e ) };
	const auto thumbnail { co_await thumbnailers.front()->createThumbnailFile( std::move( call ), size, size ) };
	if ( !thumbnail )
		co_return http::Response::error( 500, std::format( "Thumbnail generation failed: {}", thumbnail.error() ) );

	const auto bytes { std::span { thumbnail->m_pixel_data } };
	const bool cacheable { getThumbnailCachingEnabled() && thumbnail->cache_thumbnail
		                   && std::ranges::contains( getCacheableThumbnailSizes(), size ) };
	if ( !cacheable )
	{
		auto response {
			http::Response::bytes( 200, folly::IOBuf::copyBuffer( bytes.data(), bytes.size() ), "image/webp" )
		};
		response.message.getHeaders().set( "Cache-Control", "no-store" );
		co_return response;
	}

	const auto cached { co_await cacheThumbnail( path, bytes ) };
	if ( !cached )
		co_return http::Response::error( 500, std::format( "Thumbnail cache write failed: {}", cached.error() ) );

	co_return co_await serveCachedThumbnail( context, path, hex );
}

http::HTTPTask< http::EncodedResult< http::RequestBody, "image/webp" > > fetchThumbnail(
	db::DBContext& db,
	http::RequestContext context,
	http::Query< FetchThumbnailQuery > query )
{
	const auto record_id { *context.parameters().scalar< RecordID >( "record_id" ) };

	co_return http::encodedResponse< http::RequestBody, "image/webp" >(
		co_await recordThumbnailResponse( db, context, record_id, query.value ) );
}

} // namespace idhan::httpapi
