#include "clusters.hpp"

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <folly/io/IOBuf.h>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <optional>
#include <simdjson.h>
#include <string>
#include <string_view>
#include <system_error>

#include "Config.hpp"
#include "IDHANTypes.hpp"
#include "apiAuth.hpp"
#include "clusterScan.hpp"
#include "db/findClusterByName.hpp"
#include "filesystem/clusters/ClusterManager.hpp"
#include "filesystem/clusters/ClusterMarker.hpp"
#include "filesystem/clusters/ClusterWrite.hpp"
#include "filesystem/clusters/clusterIdentity.hpp"
#include "idhan/db/DBContext.hpp"
#include "idhan/db/DBError.hpp"
#include "jobs/JobContext.hpp"
#include "jsonBody.hpp"

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

[[nodiscard]] static std::expected< ClusterFields, http::Response > parseClusterFields( const std::string& text )
{
	simdjson::dom::parser parser;
	try
	{
		const simdjson::dom::element root { parser.parse( text ) };
		if ( !root.is_object() )
			return std::unexpected( http::Response::error( 400, "Invalid json object. Expected object as root item" ) );

		ClusterFields result {};

		std::string_view path_value {};
		if ( root[ "path" ].get_string().get( path_value ) == simdjson::SUCCESS )
			result.path = std::string { path_value };

		bool readonly_value {};
		if ( root[ "readonly" ].get_bool().get( readonly_value ) == simdjson::SUCCESS )
			result.readonly = readonly_value;

		std::string_view name_value {};
		if ( root[ "name" ].get_string().get( name_value ) == simdjson::SUCCESS )
			result.name = std::string { name_value };

		std::int64_t ratio_value {};
		if ( root[ "ratio" ].get_int64().get( ratio_value ) == simdjson::SUCCESS )
		{
			if ( ratio_value < 0 || ratio_value > std::numeric_limits< std::int16_t >::max() )
				return std::unexpected(
					http::Response::error(
						400,
						std::format(
							"ratio must be between 0 and {}, got {}",
							std::numeric_limits< std::int16_t >::max(),
							ratio_value ) ) );
			result.ratio = static_cast< std::int16_t >( ratio_value );
		}

		simdjson::dom::element size_element {};
		if ( root[ "size" ].get( size_element ) == simdjson::SUCCESS && size_element.is_object() )
		{
			std::int64_t limit_value {};
			if ( size_element[ "limit" ].get_int64().get( limit_value ) == simdjson::SUCCESS )
			{
				if ( limit_value < 0 )
					return std::unexpected(
						http::Response::error(
							400, std::format( "size limit cannot be negative, got {}", limit_value ) ) );
				result.size_limit = limit_value;
			}
		}

		return result;
	}
	catch ( const simdjson::simdjson_error& e )
	{
		return std::unexpected( http::Response::error( 400, e.what() ) );
	}
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
			.query( "UPDATE file_clusters SET read_only = $1 WHERE cluster_id = $2", std::move( bindings ) )
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
					"UPDATE file_clusters SET cluster_name = $1 WHERE cluster_id = $2", std::move( update_bindings ) )
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
			.query( "UPDATE file_clusters SET ratio_number = $1 WHERE cluster_id = $2", std::move( bindings ) )
			.result();
	}

	if ( fields.size_limit )
	{
		db::DBBindings bindings {};
		bindings.bind( *fields.size_limit );
		bindings.bind( cluster_id );
		co_await transaction
			.query( "UPDATE file_clusters SET size_limit = $1 WHERE cluster_id = $2", std::move( bindings ) )
			.result();
	}

	co_return std::nullopt;
}

[[nodiscard]] static http::PathParameter clusterIdPathParameter()
{
	return http::PathParameter {
		"cluster_id",
		[]( const std::string_view value ) -> std::expected< void, http::ParseError >
		{
			ClusterID id {};
			const auto result { std::from_chars( value.data(), value.data() + value.size(), id ) };
			if ( result.ec != std::errc {} || result.ptr != value.data() + value.size() || id <= 0 )
				return std::unexpected( http::ParseError { "cluster_id", "must be a positive cluster id" } );
			return {};
		}
	};
}

static http::HTTPTask< std::optional< std::string > > clusterInfoJson( const ClusterID cluster_id, db::DBContext& db )
{
	const auto configuration { filesystem::ClusterManager::getInstance().find( cluster_id ) };
	if ( !configuration ) co_return std::nullopt;
	db::DBBindings bindings {};
	bindings.bind( cluster_id );
	const auto rows {
		co_await db
			.query(
				"SELECT size_used, file_count::bigint FROM file_clusters WHERE cluster_id = $1", std::move( bindings ) )
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
				"SELECT m.name AS mime, COUNT(fi.record_id) AS cnt, COALESCE(SUM(fi.size), 0)::BIGINT AS bytes "
				"FROM file_info fi LEFT JOIN mime m ON fi.mime_id = m.mime_id "
				"WHERE fi.cluster_id = $1 GROUP BY m.name ORDER BY bytes DESC",
				std::move( mime_bindings ) )
			.rows< std::optional< std::string >, std::int64_t, std::int64_t >()
	};

	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.append_key_value( "cluster_id", cluster_id );
	builder.append_comma();
	builder.append_key_value( "readonly", read_only );
	builder.append_comma();
	builder.append_key_value( "name", cluster_name );
	builder.append_comma();
	builder.escape_and_append_with_quotes( "size" );
	builder.append_colon();
	builder.start_object();
	builder.append_key_value( "used", size_used );
	builder.append_comma();
	builder.append_key_value( "limit", size_limit );
	builder.append_comma();
	builder.append_key_value(
		"available", size_limit == 0 ? std::numeric_limits< std::int64_t >::max() : size_limit - size_used );
	builder.end_object();
	builder.append_comma();
	builder.append_key_value( "file_count", file_count );
	builder.append_comma();
	builder.append_key_value( "ratio_number", ratio_number );
	builder.append_comma();
	builder.append_key_value( "path", folder_path );
	builder.append_comma();
	builder.escape_and_append_with_quotes( "by_mime" );
	builder.append_colon();
	builder.start_array();

	bool first { true };
	for ( const auto& [ mime, count, bytes ] : mime_rows )
	{
		if ( !first ) builder.append_comma();
		first = false;

		builder.start_object();
		if ( mime )
		{
			builder.append_key_value( "mime", *mime );
		}
		else
		{
			builder.escape_and_append_with_quotes( "mime" );
			builder.append_colon();
			builder.append_raw( "null" );
		}
		builder.append_comma();
		builder.append_key_value( "count", count );
		builder.append_comma();
		builder.append_key_value( "bytes", bytes );
		builder.end_object();
	}

	builder.end_array();
	builder.end_object();

	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	co_return std::string { *json };
}

static http::HTTPTask< http::Response > clusterInfo( db::DBContext& db, http::RequestContext context )
{
	const auto cluster_id_e { context.parameters().scalar< ClusterID >( "cluster_id" ) };
	if ( !cluster_id_e ) co_return http::Response::error( 400, cluster_id_e.error().message );

	const auto info { co_await clusterInfoJson( *cluster_id_e, db ) };
	if ( !info ) co_return http::Response::error( 404, std::format( "Cluster {} does not exist", *cluster_id_e ) );

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( *info ) );
}

static http::HTTPTask< http::Response > listClusters( db::DBContext& db, [[maybe_unused]] http::RequestContext context )
{
	const auto clusters { filesystem::ClusterManager::getInstance().list() };

	simdjson::builder::string_builder builder;
	builder.start_array();

	bool first { true };
	for ( const auto& cluster : clusters )
	{
		const auto info { co_await clusterInfoJson( cluster.id, db ) };
		if ( !info ) continue;

		if ( !first ) builder.append_comma();
		first = false;
		builder.append_raw( *info );
	}

	builder.end_array();

	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( *json ) );
}

static http::HTTPTask< http::Response > addCluster( db::DBContext& db, http::RequestContext context )
{
	auto body_e { readValidatedJsonBody( context ) };
	if ( !body_e ) co_return std::move( body_e.error() );

	auto fields_e { parseClusterFields( *body_e ) };
	if ( !fields_e ) co_return std::move( fields_e.error() );
	auto& fields { *fields_e };

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
		co_await transaction.query( "SELECT folder_path FROM file_clusters" ).rows< std::string >()
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
		auto marker_e { filesystem::readClusterMarker( root->fd() ) };
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
				"INSERT INTO file_clusters (cluster_name, folder_path, cluster_uuid) VALUES ($1, $2, $3::uuid) "
				"ON CONFLICT DO NOTHING RETURNING cluster_id",
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

	const auto info { co_await clusterInfoJson( cluster_id, db ) };
	if ( !info )
		co_return http::Response::error(
			500, std::format( "Cluster {} was created but info lookup failed", cluster_id ) );

	co_return http::Response::json( 201, folly::IOBuf::copyBuffer( *info ) );
}

static http::HTTPTask< http::Response > modifyCluster( db::DBContext& db, http::RequestContext context )
{
	const auto cluster_id_e { context.parameters().scalar< ClusterID >( "cluster_id" ) };
	if ( !cluster_id_e ) co_return http::Response::error( 400, cluster_id_e.error().message );
	const auto cluster_id { *cluster_id_e };

	auto body_e { readValidatedJsonBody( context ) };
	if ( !body_e ) co_return std::move( body_e.error() );

	auto fields_e { parseClusterFields( *body_e ) };
	if ( !fields_e ) co_return std::move( fields_e.error() );
	const auto& fields { *fields_e };

	auto mutation { co_await filesystem::ClusterManager::getInstance().beginWrite( db ) };
	auto& transaction { mutation.transaction() };

	db::DBBindings exists_bindings {};
	exists_bindings.bind( cluster_id );
	const auto cluster_info {
		co_await transaction
			.query( "SELECT cluster_id FROM file_clusters WHERE cluster_id = $1", std::move( exists_bindings ) )
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

	const auto info { co_await clusterInfoJson( cluster_id, db ) };
	if ( !info ) co_return http::Response::error( 404, std::format( "Cluster {} does not exist", cluster_id ) );

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( *info ) );
}

static http::HTTPTask< http::Response > deleteCluster( db::DBContext& db, http::RequestContext context )
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
				"SELECT cluster_id FROM file_clusters WHERE cluster_id = $1 FOR UPDATE", std::move( lock_bindings ) )
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
		.query( "UPDATE file_info SET cluster_id = NULL WHERE cluster_id = $1", std::move( detach_bindings ) )
		.result();

	db::DBBindings delete_bindings {};
	delete_bindings.bind( cluster_id );
	co_await transaction.query( "DELETE FROM file_clusters WHERE cluster_id = $1", std::move( delete_bindings ) )
		.result();

	co_await mutation.commit();

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( std::string_view { "{}" } ) );
}

static http::HTTPTask< http::Response > scanCluster( db::DBContext& db, http::RequestContext context )
{
	const auto cluster_id_e { context.parameters().scalar< ClusterID >( "cluster_id" ) };
	if ( !cluster_id_e ) co_return http::Response::error( 400, cluster_id_e.error().message );
	const auto cluster_id { *cluster_id_e };

	ClusterScanParams scan_params {};

	const auto force_readonly_e { context.parameters().optional< bool >( "readonly" ) };
	if ( !force_readonly_e ) co_return http::Response::error( 400, force_readonly_e.error().message );
	scan_params.force_readonly = force_readonly_e->value_or( false );

	const auto verify_hash_e { context.parameters().optional< bool >( "verify_hash" ) };
	if ( !verify_hash_e ) co_return http::Response::error( 400, verify_hash_e.error().message );
	scan_params.verify_hash = verify_hash_e->value_or( false );

	const auto adopt_orphans_e { context.parameters().optional< bool >( "adopt_orphans" ) };
	if ( !adopt_orphans_e ) co_return http::Response::error( 400, adopt_orphans_e.error().message );
	scan_params.adopt_orphans = adopt_orphans_e->value_or( false );

	const auto scan_mime_e { context.parameters().optional< bool >( "scan_mime" ) };
	if ( !scan_mime_e ) co_return http::Response::error( 400, scan_mime_e.error().message );
	scan_params.scan_mime = scan_mime_e->value_or( true );

	const auto rescan_mime_e { context.parameters().optional< bool >( "rescan_mime" ) };
	if ( !rescan_mime_e ) co_return http::Response::error( 400, rescan_mime_e.error().message );
	scan_params.rescan_mime = rescan_mime_e->value_or( false );

	const auto scan_metadata_e { context.parameters().optional< bool >( "scan_metadata" ) };
	if ( !scan_metadata_e ) co_return http::Response::error( 400, scan_metadata_e.error().message );
	scan_params.scan_metadata = scan_metadata_e->value_or( true );

	const auto rescan_metadata_e { context.parameters().optional< bool >( "rescan_metadata" ) };
	if ( !rescan_metadata_e ) co_return http::Response::error( 400, rescan_metadata_e.error().message );
	scan_params.rescan_metadata = rescan_metadata_e->value_or( false );

	const auto stop_on_fail_e { context.parameters().optional< bool >( "stop_on_fail" ) };
	if ( !stop_on_fail_e ) co_return http::Response::error( 400, stop_on_fail_e.error().message );
	scan_params.stop_on_fail = stop_on_fail_e->value_or( false );

	const auto track_missing_files_e { context.parameters().optional< bool >( "remove_missing_files" ) };
	if ( !track_missing_files_e ) co_return http::Response::error( 400, track_missing_files_e.error().message );
	scan_params.track_missing_files = track_missing_files_e->value_or( false );

	const auto fix_extensions_e { context.parameters().optional< bool >( "fix_extensions" ) };
	if ( !fix_extensions_e ) co_return http::Response::error( 400, fix_extensions_e.error().message );
	scan_params.fix_extensions = fix_extensions_e->value_or( false );

	scan_params.scan_metadata |= scan_params.adopt_orphans;
	scan_params.scan_mime |= scan_params.scan_metadata;

	constexpr std::size_t max_concurrency { 64 };
	const auto configured_concurrency {
		config::getSilentDefault< std::size_t >( "cluster", "scan_concurrency", std::size_t { 4 } )
	};

	const auto concurrency_e { context.parameters().optional< std::size_t >( "concurrency" ) };
	if ( !concurrency_e ) co_return http::Response::error( 400, concurrency_e.error().message );
	scan_params.concurrency =
		std::clamp( concurrency_e->value_or( configured_concurrency ), std::size_t { 1 }, max_concurrency );

	const auto info { filesystem::ClusterManager::getInstance().find( cluster_id ) };
	if ( !info ) co_return http::Response::error( 404, "Cluster not found" );
	scan_params.read_only = scan_params.force_readonly || info->read_only;
	const auto cluster_path { info->path };

	auto verified { co_await filesystem::openCluster( db, cluster_id ) };
	if ( !verified ) co_return http::Response::error( 503, verified.error() );

	const auto job_ctx { queueJob(
		scanClusterJob( cluster_id, cluster_path, scan_params, db ),
		std::format( "Scanning cluster {}", cluster_id ) ) };

	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.append_key_value( "job_id", job_ctx->id() );
	builder.end_object();

	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( *json ) );
}

std::vector< http::RouteDefinition > clusterRoutes( db::DBContext& db )
{
	std::vector< http::RouteDefinition > routes {};

	const auto add =
		[ &routes, &db ](
			const proxygen::HTTPMethod method,
			std::string path,
			std::vector< http::PathParameter > path_parameters,
			http::HTTPTask< http::Response > ( *handler )( db::DBContext&, http::RequestContext ) )
	{
		auto bound = [ &db, handler ]( http::RequestContext context ) -> http::HTTPTask< http::Response >
		{ co_return co_await handler( db, std::move( context ) ); };

		routes.push_back(
			http::RouteDefinition {
				http::RouteMetadata {
					method, std::move( path ), std::move( path_parameters ), { apiAuthValidation( db ) } },
				std::move( bound ) } );
	};

	add( proxygen::HTTPMethod::GET, "/clusters/list", {}, listClusters );
	add( proxygen::HTTPMethod::GET, "/clusters/{cluster_id}/info", { clusterIdPathParameter() }, clusterInfo );
	add( proxygen::HTTPMethod::POST, "/clusters/add", {}, addCluster );
	add( proxygen::HTTPMethod::PATCH, "/clusters/{cluster_id}/modify", { clusterIdPathParameter() }, modifyCluster );
	add( proxygen::HTTPMethod::DELETE, "/clusters/{cluster_id}/remove", { clusterIdPathParameter() }, deleteCluster );
	add( proxygen::HTTPMethod::POST, "/clusters/{cluster_id}/scan", { clusterIdPathParameter() }, scanCluster );

	return routes;
}

} // namespace idhan::httpapi
