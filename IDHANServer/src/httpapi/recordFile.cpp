#include "recordFile.hpp"

#include <sys/stat.h>

#include <chrono>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <vector>

#include "IDHANTypes.hpp"
#include "apiAuth.hpp"
#include "filesystem/PathHelpers.hpp"
#include "filesystem/clusters/clusterIdentity.hpp"
#include "idhan/SHA256.hpp"
#include "idhan/db/DBContext.hpp"
#include "idhan/http/FileResponse.hpp"
#include "recordValidator.hpp"

namespace idhan::httpapi
{

[[nodiscard]] static std::string sanitizeFilename( const std::string_view name )
{
	std::string result { name };
	for ( char& character : result )
	{
		const auto byte { static_cast< unsigned char >( character ) };
		const bool alphanumeric {
			( byte >= 'a' && byte <= 'z' ) || ( byte >= 'A' && byte <= 'Z' ) || ( byte >= '0' && byte <= '9' )
		};
		if ( !alphanumeric && character != '.' && character != '-' && character != '_' ) character = '_';
	}
	return result;
}

http::HTTPTask< http::Response > recordFileResponse(
	db::DBContext& db,
	http::RequestContext& context,
	const RecordID record_id )
{
	const auto download_e { context.parameters().optional< bool >( "download" ) };
	if ( !download_e ) co_return http::Response::error( 400, download_e.error().message );

	db::DBBindings bindings {};
	bindings.bind( record_id );

	auto rows {
		co_await db
			.query(
				"SELECT file_clusters.cluster_id, folder_path, sha256, "
				"COALESCE(file_info.extension, mime.best_extension, '') AS extension, mime.name AS mime_name "
				"FROM records "
				"JOIN file_info ON records.record_id = file_info.record_id "
				"LEFT JOIN mime ON file_info.mime_id = mime.mime_id "
				"JOIN file_clusters ON file_clusters.cluster_id = file_info.cluster_id "
				"WHERE records.record_id = $1",
				std::move( bindings ) )
			.rows< ClusterID, std::string, std::span< const std::byte >, std::string, std::optional< std::string > >()
	};

	if ( rows.empty() )
		co_return http::Response::error( 404, std::format( "Record {} is not stored in any cluster", record_id ) );

	const auto& [ cluster_id, folder_path, sha256_bytes, extension, mime_name ] { rows.row( 0 ) };

	auto cluster { co_await filesystem::openCluster( db, cluster_id ) };
	if ( !cluster ) co_return http::Response::error( 503, cluster.error() );

	const SHA256 sha256 { SHA256::fromBuffer( sha256_bytes ) };
	const auto hex { sha256.hex() };
	const auto relative_path { filesystem::getClusterRelativePath( sha256, extension ) };
	const auto filename { relative_path.filename().string() };
	const auto file_path { cluster->root() / relative_path };

	auto file_e { folly::File::makeFile( file_path.c_str(), O_RDONLY ) };
	if ( !file_e )
		co_return http::Response::error(
			500, std::format( "Record {} could not be opened at '{}'", record_id, file_path.string() ) );

	auto file { std::move( *file_e ) };

	struct stat status {};
	if ( ::fstat( file.fd(), &status ) != 0 )
		co_return http::Response::error(
			500, std::format( "Record {} could not be inspected at '{}'", record_id, file_path.string() ) );

	if ( !S_ISREG( status.st_mode ) )
		co_return http::Response::error(
			500, std::format( "Record {} is not a regular file at '{}'", record_id, file_path.string() ) );

	http::FileMetadata metadata {
		.size = static_cast< std::uint64_t >( status.st_size ),
		.modified = std::chrono::system_clock::from_time_t( status.st_mtime ),
		.changed = std::chrono::system_clock::from_time_t( status.st_ctime ),
		.etag = hex,
		.content_type = mime_name.value_or( "application/octet-stream" ),
		.filename = sanitizeFilename( filename ),
		.immutable = true,
		.attachment = download_e->value_or( false ),
	};

	co_return co_await http::FileResponse::create( context, std::move( file ), std::move( metadata ) );
}

static http::HTTPTask< http::Response > fetchRecordFile( db::DBContext& db, http::RequestContext context )
{
	const auto record_id_e { context.parameters().scalar< RecordID >( "record_id" ) };
	if ( !record_id_e ) co_return http::Response::error( 400, record_id_e.error().message );

	co_return co_await recordFileResponse( db, context, *record_id_e );
}

http::RouteDefinition recordFileRoute( db::DBContext& db, std::string path )
{
	auto handler = [ &db ]( http::RequestContext context ) -> http::HTTPTask< http::Response >
	{ co_return co_await fetchRecordFile( db, std::move( context ) ); };

	return http::RouteDefinition {
		http::RouteMetadata {
			proxygen::HTTPMethod::GET,
			std::move( path ),
			{ recordIdPathParameter() },
			{ apiAuthValidation( db ), recordExistsValidation( db ) } },
		std::move( handler )
	};
}

} // namespace idhan::httpapi
