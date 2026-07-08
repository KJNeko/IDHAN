//
// Created by kj16609 on 11/18/24.
//

#include <fstream>

#include "filesystem/clusters/ClusterManager.hpp"
#include "api/ClusterAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "logging/log.hpp"

namespace idhan::api
{

namespace
{
// Normalizes a path so that equivalent spellings ("/a/b/", "/a/b/.", "/a/b") compare equal
std::filesystem::path normalizeClusterPath( std::filesystem::path path )
{
	path = path.lexically_normal();
	// lexically_normal keeps a trailing separator; strip it so "/a/b/" and "/a/b" match
	if ( !path.has_filename() ) path = path.parent_path();
	return path;
}
} // namespace

ClusterAPI::ResponseTask ClusterAPI::add( drogon::HttpRequestPtr request )
{
	auto db { drogon::app().getDbClient() };

	auto transaction { co_await db->newTransactionCoro() };

	const auto& request_json_ptr { request->getJsonObject() };

	if ( request_json_ptr == nullptr ) co_return createBadRequest( "No json data supplied" );

	const auto& request_json { *request_json_ptr };

	// operator[] on a non-object root throws Json::LogicError, which would surface as a 500
	if ( !request_json.isObject() ) co_return createBadRequest( "Invalid json object. Expected object as root item" );

	if ( !request_json[ "path" ].isString() || request_json[ "path" ].asString().empty() )
		co_return createBadRequest( "Cluster path must be specified as a non-empty string" );

	std::filesystem::path target_path { request_json[ "path" ].asString() };
	if ( target_path.is_relative() ) target_path = std::filesystem::absolute( target_path );
	target_path = normalizeClusterPath( target_path );

	const bool readonly { request_json[ "readonly" ].isBool() ? request_json[ "readonly" ].asBool() : true };

	if ( readonly && !std::filesystem::exists( target_path ) )
	{
		co_return createBadRequest(
			"Path {} does not exist, but was requested as read only. This seems wrong", target_path.string() );
	}

	{
		// Test if the path we are wanting to add is already
		const auto folder_search {
			co_await transaction->execSqlCoro( "SELECT cluster_id, folder_path FROM file_clusters" )
		};

		for ( const auto& row : folder_search )
		{
			// each row will have cluster_id, folder_path
			[[maybe_unused]] const auto cluster_id { row[ 0 ].as< ClusterID >() };
			const auto cluster_path { row[ 1 ].as< std::string >() };

			const std::filesystem::path path { normalizeClusterPath( cluster_path ) };
			if ( target_path == path )
			{
				transaction->rollback();

				co_return createConflict( "Path {} already exists in the cluster list", path.string() );
			}
		}
	}

	log::debug( "Found no conflicting paths" );

	// The path was not already in use. Now check if it's valid
	if ( !std::filesystem::exists( target_path ) )
	{
		// Since we already have a check that we are not read only, we should try creating it
		if ( !std::filesystem::create_directories( target_path ) )
		{
			transaction->rollback();

			co_return createInternalError( "Was unable to create directory {}", target_path.string() );
		}
	}

	if ( !readonly )
	{
		// We can make the directory or it already exists.
		// Can we write into it?
		if ( std::ofstream ofs( target_path / "write_test.txt" ); ofs )
		{
			constexpr std::string_view test_string { "IDHAN can write" };
			ofs.write( test_string.data(), test_string.size() );
		}
		else
		{
			transaction->rollback();
			co_return createInternalError(
				"Failed to write to file {} as write test for cluster", ( target_path / "write_test.txt" ).string() );
		}

		log::debug( "Write test passed. Inserting new cluster into table" );

		// delete the test
		std::filesystem::remove( target_path / "write_test.txt" );
	}

	if ( !request_json[ "name" ].isString() )
	{
		transaction->rollback();
		co_return createBadRequest( "Cluster name must be specified" );
	}
	const std::string cluster_name { request_json[ "name" ].asString() };

	try
	{
		// the PG backend throws no typed exception for unique violations (SqlError with
		// sqlState() is MySQL/SQLite-only), so detect the conflict via the empty result
		// instead. cluster_name and folder_path both carry UNIQUE constraints.
		const auto insert_result { co_await transaction->execSqlCoro(
			"INSERT INTO file_clusters ( cluster_name, folder_path ) VALUES ($1, $2) "
			"ON CONFLICT DO NOTHING RETURNING cluster_id",
			cluster_name,
			target_path.string() ) };

		if ( insert_result.empty() )
		{
			transaction->rollback();

			co_return createConflict(
				"A cluster with the name {} or path {} already exists", cluster_name, target_path.string() );
		}

		const auto cluster_id { insert_result[ 0 ][ 0 ].as< ClusterID >() };

		log::debug( "Setting cluster info" );

		// Modify will return `{cluster_id}/list` if it succeeds.
		const auto ret { co_await modifyT( request, cluster_id, transaction ) };

		// TODO: Queue orphan check here.
		co_await filesystem::ClusterManager::getInstance().reloadClusters( transaction );

		co_return ret;
	}
	catch ( std::exception& e )
	{
		transaction->rollback();

		co_return createInternalError( "Failed to insert cluster into table: {}", e.what() );
	}
}

} // namespace idhan::api
