#include "clusters.hpp"

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "Config.hpp"
#include "IDHANTypes.hpp"
#include "clusterScan.hpp"
#include "db/findClusterByName.hpp"
#include "filesystem/clusters/ClusterManager.hpp"
#include "filesystem/clusters/ClusterMarker.hpp"
#include "filesystem/clusters/ClusterWrite.hpp"
#include "filesystem/clusters/clusterIdentity.hpp"
#include "idhan/db/DBContext.hpp"
#include "idhan/db/DBError.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"
#include "jobs/JobContext.hpp"

namespace idhan::httpapi
{

[[nodiscard]] static std::filesystem::path normalizeClusterPath( std::filesystem::path path )
{
	path = std::filesystem::absolute( path );
	path = path.lexically_normal();
	if ( !path.has_filename() ) path = path.parent_path();
	return path;
}

struct ClusterFields
{
	std::optional< std::string > path {};
	std::optional< bool > readonly {};
	std::optional< std::string > name {};
	std::optional< std::int16_t > ratio {};
	std::optional< std::int64_t > size_limit {};
};

[[nodiscard]] static ClusterFields clusterFields( const ModifyClusterBody& body )
{
	ClusterFields result {};
	result.path = body.path;
	result.readonly = body.readonly;
	result.name = body.name;
	result.ratio = body.ratio;
	if ( body.size ) result.size_limit = body.size->limit;

	return result;
}

[[nodiscard]] static ClusterFields clusterFields( const AddClusterBody& body )
{
	ClusterFields result {};
	result.path = body.path;
	result.readonly = body.readonly;
	result.name = body.name;
	result.ratio = body.ratio;
	if ( body.size ) result.size_limit = body.size->limit;

	return result;
}

[[nodiscard]] static http::HTTPTask< std::optional< http::Response > > applyClusterFields(
	db::DBTransaction& transaction,
	const ClusterID cluster_id,
	const ClusterFields& fields )
{
	if ( fields.readonly )
	{
		db::DBBindings bindings {};
		bindings.bind( *fields.readonly );
		bindings.bind( cluster_id );
		co_await transaction
				 .query(
					 //language=postgresql
					 R"SQL(
					UPDATE
					    file_clusters
					SET read_only = $1
					WHERE cluster_id = $2
				)SQL",
					 std::move( bindings ) )
				 .result();
	}

	if ( fields.name )
	{
		const auto existing { co_await db::findClusterByName( transaction, *fields.name ) };
		if ( existing && *existing != cluster_id )
			co_return http::Response::error(
				409, std::format( "A cluster with the name {} already exists", *fields.name ) );

		db::DBBindings update_bindings {};
		update_bindings.bind( *fields.name );
		update_bindings.bind( cluster_id );
		try
		{
			co_await transaction
				.query(
					//language=postgresql
					R"SQL(
						UPDATE
						    file_clusters
						SET cluster_name = $1
						WHERE cluster_id = $2
					)SQL",
					std::move( update_bindings ) )
				.result();
		}
		catch ( const db::DBConstraintError& error )
		{
			const auto& diagnostics { error.diagnostics() };
			if ( diagnostics.sqlstate != "23505" ) throw;
			if ( diagnostics.constraint != "file_clusters_cluster_name_key" ) throw;
			co_return http::Response::error(
				409, std::format( "A cluster with the name {} already exists", *fields.name ) );
		}
	}

	if ( fields.ratio )
	{
		db::DBBindings bindings {};
		bindings.bind( *fields.ratio );
		bindings.bind( cluster_id );
		co_await transaction
				 .query(
					 //language=postgresql
					 R"SQL(
					UPDATE
					    file_clusters
					SET ratio_number = $1
					WHERE cluster_id = $2
				)SQL",
					 std::move( bindings ) )
				 .result();
	}

	if ( fields.size_limit )
	{
		db::DBBindings bindings {};
		bindings.bind( *fields.size_limit );
		bindings.bind( cluster_id );
		co_await transaction
				 .query(
					 //language=postgresql
					 R"SQL(
					UPDATE
					    file_clusters
					SET size_limit = $1
					WHERE cluster_id = $2
				)SQL",
					 std::move( bindings ) )
				 .result();
	}

	co_return std::nullopt;
}

static http::HTTPTask< std::optional< ClusterInfoResponse > > readClusterInfo(
	const ClusterID cluster_id,
	db::DBContext& db )
{
	const auto configuration { filesystem::ClusterManager::getInstance().find( cluster_id ) };
	if ( !configuration ) co_return std::nullopt;
	db::DBBindings bindings {};
	bindings.bind( cluster_id );
	const auto rows {
		co_await db
			.query(
				//language=postgresql
				R"SQL(
					SELECT
					    size_used,
					    file_count::bigint
					FROM file_clusters
					WHERE cluster_id = $1
				)SQL",
				std::move( bindings ) )
			.rows< std::int64_t, std::int64_t >()
	};
	if ( rows.empty() ) co_return std::nullopt;
	const auto& [ size_used, file_count ] { rows.row( 0 ) };
	const auto ratio_number { configuration->ratio };
	const auto size_limit { configuration->size_limit };
	const auto read_only { configuration->read_only };
	const auto folder_path { configuration->path.string() };
	const auto cluster_name { configuration->name.value_or( folder_path ) };

	db::DBBindings mime_bindings {};
	mime_bindings.bind( cluster_id );
	const auto mime_rows {
		co_await db
			.query(
				//language=postgresql
				R"SQL(
					SELECT
					    m.name AS mime,
					    COUNT(fi.record_id) AS cnt,
					    COALESCE(SUM(fi.size), 0)::BIGINT AS bytes
					FROM file_info fi
					    LEFT JOIN mime m ON fi.mime_id = m.mime_id
					WHERE fi.cluster_id = $1
					GROUP BY
					    m.name
					ORDER BY
					    bytes DESC
				)SQL",
				std::move( mime_bindings ) )
			.rows< std::optional< std::string >, std::int64_t, std::int64_t >()
	};

	auto available { std::numeric_limits< std::int64_t >::max() };
	if ( size_limit != 0 ) available = size_used >= size_limit ? 0 : size_limit - size_used;

	ClusterInfoResponse result { cluster_id, read_only,    cluster_name, { size_used, size_limit, available },
	                             file_count, ratio_number, folder_path,  {} };
	result.by_mime.reserve( mime_rows.size() );
	for ( const auto& [ mime, count, bytes ] : mime_rows ) result.by_mime.push_back( { mime, count, bytes } );

	co_return result;
}

http::HTTPTask< http::JsonResult< ClusterInfoResponse > > clusterInfo( db::DBContext& db, http::RequestContext context )
{
	const auto cluster_id_e { context.parameters().scalar< ClusterID >( "cluster_id" ) };
	if ( !cluster_id_e ) co_return http::Response::error( 400, cluster_id_e.error().message );

	const auto info { co_await readClusterInfo( *cluster_id_e, db ) };
	if ( !info ) co_return http::Response::error( 404, std::format( "Cluster {} does not exist", *cluster_id_e ) );

	co_return http::JsonResponse< ClusterInfoResponse > { *info };
}

http::HTTPTask< http::JsonResult< std::vector< ClusterInfoResponse > > > listClusters(
	db::DBContext& db,
	[[maybe_unused]] http::RequestContext context )
{
	const auto clusters { filesystem::ClusterManager::getInstance().list() };

	std::vector< ClusterInfoResponse > response;
	response.reserve( clusters.size() );
	for ( const auto& cluster : clusters )
	{
		auto info { co_await readClusterInfo( cluster.id, db ) };
		if ( !info ) continue;

		response.push_back( std::move( *info ) );
	}

	co_return http::JsonResponse< std::vector< ClusterInfoResponse > > { std::move( response ) };
}

http::HTTPTask< http::JsonResult< ClusterInfoResponse, 201 > > addCluster(
	db::DBContext& db,
	http::JsonBody< AddClusterBody > body )
{
	const auto fields { clusterFields( body.value ) };

	if ( !fields.path || fields.path->empty() )
		co_return http::Response::error( 400, "Cluster path must be specified as a non-empty string" );

	if ( !fields.name || fields.name->empty() )
		co_return http::Response::error( 400, "Cluster name must be specified" );

	const auto target_path { normalizeClusterPath( *fields.path ) };
	auto mutation { co_await filesystem::ClusterManager::getInstance().beginWrite( db ) };
	auto& transaction { mutation.transaction() };
	const auto existing_name { co_await db::findClusterByName( transaction, *fields.name ) };
	if ( existing_name )
	{
		co_await mutation.rollback();
		co_return http::Response::error(
			409, std::format( "A cluster with the name {} already exists", *fields.name ) );
	}

	const auto folder_search {
		co_await transaction
				 .query(
					 //language=postgresql
					 R"SQL(
					SELECT
					    folder_path
					FROM file_clusters
				)SQL" )
				 .rows< std::string >()
	};

	for ( const auto& [ cluster_path ] : folder_search )
	{
		std::error_code equivalent_error {};
		const bool same_directory { std::filesystem::equivalent( target_path, cluster_path, equivalent_error ) };
		if ( target_path == normalizeClusterPath( cluster_path ) || same_directory )
		{
			co_await mutation.rollback();
			co_return http::Response::error(
				409, std::format( "Path {} already exists in the cluster list", target_path.string() ) );
		}
	}

	const bool readonly { fields.readonly.value_or( true ) };
	if ( readonly && !std::filesystem::exists( target_path ) )
	{
		co_await mutation.rollback();
		co_return http::Response::error(
			400,
			std::format(
				"Path {} does not exist, but was requested as read only. This seems wrong", target_path.string() ) );
	}

	if ( !std::filesystem::exists( target_path ) )
	{
		std::error_code error_code {};
		if ( !std::filesystem::create_directories( target_path, error_code ) )
		{
			co_await mutation.rollback();
			co_return http::Response::error(
				500, std::format( "Was unable to create directory {}", target_path.string() ) );
		}
	}

	auto root { folly::File::makeFile( target_path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC ) };
	if ( !root ) co_return http::Response::error( 500, "Could not open cluster directory for initialization" );
	auto uuid { boost::uuids::random_generator {}() };
	if ( readonly )
	{
		auto marker_e { filesystem::readClusterMarkerSync( root->fd() ) };
		if ( !marker_e.has_value() ) co_return http::Response::error( 409, marker_e.error() );

		const auto& marker { marker_e.value() };
		if ( marker ) uuid = marker->uuid;
	}
	else
	{
		auto marker_e { filesystem::initializeClusterMarker( root->fd(), uuid ) };
		if ( !marker_e.has_value() ) co_return http::Response::error( 409, marker_e.error() );

		uuid = marker_e.value().uuid;
	}

	const auto cluster_uuid { boost::uuids::to_string( uuid ) };

	db::DBBindings insert_bindings {};
	insert_bindings.bind( *fields.name );
	insert_bindings.bind( target_path.string() );
	insert_bindings.bind( cluster_uuid );
	const auto insert_result {
		co_await transaction
			.query(
				//language=postgresql
				R"SQL(
					INSERT INTO file_clusters (cluster_name, folder_path, cluster_uuid)
					    VALUES ($1, $2, $3::uuid)
					ON CONFLICT
					    DO NOTHING
					RETURNING cluster_id
				)SQL",
				std::move( insert_bindings ) )
			.rows< ClusterID >()
	};

	if ( insert_result.empty() )
	{
		co_await mutation.rollback();
		co_return http::Response::error(
			409,
			std::format(
				"A cluster with the name {}, path {}, or marker identity already exists",
				*fields.name,
				target_path.string() ) );
	}

	const auto cluster_id { std::get< 0 >( insert_result.row( 0 ) ) };

	auto modification_error { co_await applyClusterFields( transaction, cluster_id, fields ) };
	if ( modification_error )
	{
		co_await mutation.rollback();
		co_return std::move( *modification_error );
	}

	co_await mutation.commit();

	const auto info { co_await readClusterInfo( cluster_id, db ) };
	if ( !info )
		co_return http::Response::error(
			500, std::format( "Cluster {} was created but info lookup failed", cluster_id ) );

	co_return http::JsonResponse< ClusterInfoResponse, 201 > { *info };
}

http::HTTPTask< http::JsonResult< ClusterInfoResponse > > modifyCluster(
	db::DBContext& db,
	http::RequestContext context,
	http::JsonBody< ModifyClusterBody > body )
{
	const auto cluster_id_e { context.parameters().scalar< ClusterID >( "cluster_id" ) };
	if ( !cluster_id_e ) co_return http::Response::error( 400, cluster_id_e.error().message );
	const auto cluster_id { *cluster_id_e };

	const auto fields { clusterFields( body.value ) };

	auto mutation { co_await filesystem::ClusterManager::getInstance().beginWrite( db ) };
	auto& transaction { mutation.transaction() };

	db::DBBindings exists_bindings {};
	exists_bindings.bind( cluster_id );
	const auto cluster_info {
		co_await transaction
				 .query(
					 //language=postgresql
					 R"SQL(
					SELECT
					    cluster_id
					FROM file_clusters
					WHERE cluster_id = $1
				)SQL",
					 std::move( exists_bindings ) )
				 .rows< ClusterID >()
	};

	if ( cluster_info.empty() )
	{
		co_await mutation.rollback();
		co_return http::Response::error( 404, std::format( "Cluster {} does not exist", cluster_id ) );
	}

	auto verified { co_await filesystem::openCluster( db, cluster_id ) };
	if ( !verified ) co_return http::Response::error( 503, verified.error() );

	if ( fields.readonly == false && verified->readonly() )
	{
		const auto configuration { filesystem::ClusterManager::getInstance().find( cluster_id ) };
		if ( !configuration ) co_return http::Response::error( 404, "Cluster does not exist" );

		auto root { folly::File::makeFile( verified->root().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC ) };
		if ( !root ) co_return http::Response::error( 503, "Could not open cluster directory for initialization" );

		auto marker_e { filesystem::initializeClusterMarker( root->fd(), configuration->uuid ) };
		if ( !marker_e.has_value() ) co_return http::Response::error( 409, marker_e.error() );

		if ( marker_e.value().uuid != configuration->uuid )
			co_return http::Response::error( 409, "Cluster marker UUID does not match the registered identity" );
	}

	auto modification_error { co_await applyClusterFields( transaction, cluster_id, fields ) };
	if ( modification_error )
	{
		co_await mutation.rollback();
		co_return std::move( *modification_error );
	}

	co_await mutation.commit();

	const auto info { co_await readClusterInfo( cluster_id, db ) };
	if ( !info ) co_return http::Response::error( 404, std::format( "Cluster {} does not exist", cluster_id ) );

	co_return http::JsonResponse< ClusterInfoResponse > { *info };
}

http::HTTPTask< http::JsonResult< DeletedClusterResponse > > deleteCluster(
	db::DBContext& db,
	http::RequestContext context )
{
	const auto cluster_id_e { context.parameters().scalar< ClusterID >( "cluster_id" ) };
	if ( !cluster_id_e ) co_return http::Response::error( 400, cluster_id_e.error().message );
	const auto cluster_id { *cluster_id_e };

	auto mutation { co_await filesystem::ClusterManager::getInstance().beginWrite( db ) };
	auto& transaction { mutation.transaction() };

	db::DBBindings lock_bindings {};
	lock_bindings.bind( cluster_id );
	const auto locked {
		co_await transaction
			.query(
				//language=postgresql
				R"SQL(
					SELECT
					    cluster_id
					FROM file_clusters
					WHERE cluster_id = $1
					FOR UPDATE
				)SQL",
				std::move( lock_bindings ) )
			.rows< ClusterID >()
	};

	if ( locked.empty() )
	{
		co_await mutation.rollback();
		co_return http::Response::error( 404, std::format( "Cluster {} does not exist", cluster_id ) );
	}

	db::DBBindings detach_bindings {};
	detach_bindings.bind( cluster_id );
	co_await transaction
			 .query(
				 //language=postgresql
				 R"SQL(
				UPDATE
				    file_info
				SET cluster_id = NULL
				WHERE cluster_id = $1
			)SQL",
				 std::move( detach_bindings ) )
			 .result();

	db::DBBindings delete_bindings {};
	delete_bindings.bind( cluster_id );
	co_await transaction
			 .query(
				 //language=postgresql
				 R"SQL(
				DELETE FROM file_clusters
				WHERE cluster_id = $1
			)SQL",
				 std::move( delete_bindings ) )
			 .result();

	co_await mutation.commit();

	co_return http::JsonResponse< DeletedClusterResponse > {};
}

http::HTTPTask< http::JsonResult< ClusterScanResponse > > scanCluster(
	db::DBContext& db,
	http::RequestContext context,
	http::Query< ScanClusterQuery > query )
{
	const auto cluster_id_e { context.parameters().scalar< ClusterID >( "cluster_id" ) };
	if ( !cluster_id_e ) co_return http::Response::error( 400, cluster_id_e.error().message );
	const auto cluster_id { *cluster_id_e };

	const auto& parameters { query.value };
	ClusterScanParams scan_params {};
	scan_params.force_readonly = parameters.readonly.value_or( false );
	scan_params.verify_hash = parameters.verify_hash.value_or( false );
	scan_params.adopt_orphans = parameters.adopt_orphans.value_or( false );
	scan_params.scan_mime = parameters.scan_mime.value_or( true );
	scan_params.rescan_mime = parameters.rescan_mime.value_or( false );
	scan_params.scan_metadata = parameters.scan_metadata.value_or( true );
	scan_params.rescan_metadata = parameters.rescan_metadata.value_or( false );
	scan_params.stop_on_fail = parameters.stop_on_fail.value_or( false );
	scan_params.track_missing_files = parameters.remove_missing_files.value_or( false );
	scan_params.fix_extensions = parameters.fix_extensions.value_or( false );

	scan_params.scan_metadata |= scan_params.adopt_orphans;
	scan_params.scan_mime |= scan_params.scan_metadata;

	constexpr std::size_t max_concurrency { 64 };
	const auto configured_concurrency {
		config::getSilentDefault< std::size_t >( "cluster", "scan_concurrency", std::size_t { 4 } )
	};

	scan_params.concurrency =
		std::clamp( parameters.concurrency.value_or( configured_concurrency ), std::size_t { 1 }, max_concurrency );

	const auto info { filesystem::ClusterManager::getInstance().find( cluster_id ) };
	if ( !info ) co_return http::Response::error( 404, "Cluster not found" );
	scan_params.read_only = scan_params.force_readonly || info->read_only;
	const auto cluster_path { info->path };

	auto verified { co_await filesystem::openCluster( db, cluster_id ) };
	if ( !verified ) co_return http::Response::error( 503, verified.error() );

	const auto job_ctx { queueJob(
		scanClusterJob( cluster_id, cluster_path, scan_params, db ),
		std::format( "Scanning cluster {}", cluster_id ) ) };

	co_return http::JsonResponse< ClusterScanResponse > { { job_ctx->id() } };
}

} // namespace idhan::httpapi
