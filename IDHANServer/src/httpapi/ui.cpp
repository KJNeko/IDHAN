#include "ui.hpp"

#include <sys/stat.h>

#include <chrono>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <ranges>
#include <unordered_map>

#include "idhan/http/FileResponse.hpp"
#include "paths.hpp"

namespace idhan::httpapi
{

static std::expected< void, http::ParseError > validateAssetFilename( const std::string_view filename )
{
	if ( filename.empty() || filename == "." || filename == ".." )
		return std::unexpected( http::ParseError { "filename", "must name an asset file" } );

	if ( filename.find_first_of( std::string_view { "\0/\\", 3 } ) != std::string_view::npos )
		return std::unexpected( http::ParseError { "filename", "must not contain path separators" } );

	return {};
}

static std::string assetContentType( const std::string& filename )
{
	static const std::unordered_map< std::string, std::string > types {
		{ ".js", "text/javascript" },   { ".mjs", "text/javascript" },   { ".css", "text/css" },
		{ ".map", "application/json" }, { ".json", "application/json" }, { ".wasm", "application/wasm" },
		{ ".svg", "image/svg+xml" },    { ".png", "image/png" },         { ".jpg", "image/jpeg" },
		{ ".jpeg", "image/jpeg" },      { ".gif", "image/gif" },         { ".webp", "image/webp" },
		{ ".avif", "image/avif" },      { ".ico", "image/x-icon" },      { ".woff", "font/woff" },
		{ ".woff2", "font/woff2" },     { ".ttf", "font/ttf" },          { ".otf", "font/otf" },
		{ ".mp4", "video/mp4" },        { ".webm", "video/webm" }
	};
	const auto type { types.find( std::filesystem::path { filename }.extension().string() ) };
	return type == types.end() ? "application/octet-stream" : type->second;
}

static http::HTTPTask< http::Response > asset( http::RequestContext context )
{
	const auto filename { context.parameters().scalar< std::string >( "filename" ) };
	if ( !filename ) co_return http::Response::error( 400, "Invalid asset filename" );

	const auto directory { folly::File::makeFile( ( getStaticPath() / "assets" ).c_str(), O_RDONLY | O_DIRECTORY ) };
	if ( !directory ) co_return http::Response::error( 404, "Asset not found" );

	const auto descriptor {
		::openat( directory->fd(), filename->c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC )
	};
	if ( descriptor < 0 ) co_return http::Response::error( 404, "Asset not found" );

	folly::File file { descriptor, true };
	struct stat status {};
	if ( ::fstat( file.fd(), &status ) != 0 || !S_ISREG( status.st_mode ) )
		co_return http::Response::error( 404, "Asset not found" );

	http::FileMetadata metadata {
		.size = static_cast< std::uint64_t >( status.st_size ),
		.modified = std::chrono::system_clock::from_time_t( status.st_mtime ),
		.changed = std::chrono::system_clock::from_time_t( status.st_ctime ),
		.etag = std::format( "{:x}-{:x}-{:x}", status.st_mtime, status.st_mtim.tv_nsec, status.st_size ),
		.content_type = assetContentType( *filename ),
		.filename = *filename,
		.immutable = true,
	};
	co_return co_await http::FileResponse::create( context, std::move( file ), std::move( metadata ) );
}

static std::expected< void, http::ParseError > validateSubtreePath( const std::string_view relative )
{
	if ( relative.empty() ) return std::unexpected( http::ParseError { "path", "must name a file" } );

	for ( const auto component : std::views::split( relative, '/' ) )
	{
		const std::string_view segment { component.begin(), component.end() };
		if ( segment.empty() || segment == "." || segment == ".." )
			return std::unexpected( http::ParseError { "path", "must not contain relative components" } );

		if ( segment.find( '\\' ) != std::string_view::npos )
			return std::unexpected( http::ParseError { "path", "must not contain path separators" } );
	}

	return {};
}

static http::HTTPTask< http::Response > serveSubtree( const std::filesystem::path& root, http::RequestContext& context )
{
	const auto relative { context.parameters().scalar< std::string >( "path" ) };
	if ( !relative ) co_return http::Response::error( 400, "Invalid path" );

	auto directory { folly::File::makeFile( root.c_str(), O_RDONLY | O_DIRECTORY ) };
	if ( !directory ) co_return http::Response::error( 404, "Not found" );

	std::string filename {};
	for ( const auto component : std::views::split( std::string_view { *relative }, '/' ) )
	{
		const std::string segment { component.begin(), component.end() };
		if ( !filename.empty() )
		{
			auto next { folly::File::makeFile(
				::openat( directory->fd(), filename.c_str(), O_RDONLY | O_NOFOLLOW | O_DIRECTORY | O_CLOEXEC ),
				true ) };
			if ( !next || next->fd() < 0 ) co_return http::Response::error( 404, "Not found" );
			directory = std::move( next );
		}
		filename = segment;
	}

	const auto descriptor {
		::openat( directory->fd(), filename.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC )
	};
	if ( descriptor < 0 ) co_return http::Response::error( 404, "Not found" );

	folly::File file { descriptor, true };
	struct stat status {};
	if ( ::fstat( file.fd(), &status ) != 0 || !S_ISREG( status.st_mode ) )
		co_return http::Response::error( 404, "Not found" );

	http::FileMetadata metadata {
		.size = static_cast< std::uint64_t >( status.st_size ),
		.modified = std::chrono::system_clock::from_time_t( status.st_mtime ),
		.changed = std::chrono::system_clock::from_time_t( status.st_ctime ),
		.etag = std::format( "{:x}-{:x}-{:x}", status.st_mtime, status.st_mtim.tv_nsec, status.st_size ),
		.content_type = assetContentType( filename ),
		.filename = filename,
	};
	co_return co_await http::FileResponse::create( context, std::move( file ), std::move( metadata ) );
}

static http::HTTPTask< http::Response > apiDocsFile( http::RequestContext context )
{
	co_return co_await serveSubtree( getApiDocsPath(), context );
}

static http::HTTPTask< http::Response > pluginFile( http::RequestContext context )
{
	co_return co_await serveSubtree( getPluginsPath(), context );
}

static http::HTTPTask< http::Response > index( http::RequestContext context )
{
	const auto file_path { getStaticPath() / "index.html" };

	auto file_e { folly::File::makeFile( file_path.c_str(), O_RDONLY ) };
	if ( !file_e ) co_return http::Response::error( 404, "WebUI is not built into this server" );

	auto file { std::move( *file_e ) };

	struct stat status {};
	if ( ::fstat( file.fd(), &status ) != 0 || !S_ISREG( status.st_mode ) )
		co_return http::Response::error( 404, "WebUI is not built into this server" );

	http::FileMetadata metadata {
		.size = static_cast< std::uint64_t >( status.st_size ),
		.modified = std::chrono::system_clock::from_time_t( status.st_mtime ),
		.changed = std::chrono::system_clock::from_time_t( status.st_ctime ),
		.etag = std::format( "{:x}-{:x}", status.st_mtime, status.st_size ),
		.content_type = "text/html; charset=utf-8",
		.filename = "index.html",
	};

	co_return co_await http::FileResponse::create( context, std::move( file ), std::move( metadata ) );
}

http::RouteDefinition assetsRoute()
{
	return http::RouteDefinition {
		http::RouteMetadata {
			proxygen::HTTPMethod::GET, "/assets/{filename}", { { "filename", validateAssetFilename } }, {} },
		asset
	};
}

http::RouteDefinition apiDocsFilesRoute()
{
	return http::RouteDefinition {
		http::RouteMetadata { proxygen::HTTPMethod::GET, "/api/{*path}", { { "path", validateSubtreePath } }, {} },
		apiDocsFile
	};
}

http::RouteDefinition pluginFilesRoute()
{
	return http::RouteDefinition {
		http::RouteMetadata { proxygen::HTTPMethod::GET, "/plugins/{*path}", { { "path", validateSubtreePath } }, {} },
		pluginFile
	};
}

http::RouteDefinition indexRoute( std::string path )
{
	return http::RouteDefinition {
		http::RouteMetadata { proxygen::HTTPMethod::GET, std::move( path ), {}, {} }, index
	};
}

} // namespace idhan::httpapi
