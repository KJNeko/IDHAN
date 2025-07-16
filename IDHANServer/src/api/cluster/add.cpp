//
// Created by kj16609 on 11/18/24.
//

#include <fstream>

#include "api/ClusterAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "exceptions.hpp"
#include "filesystem/ClusterManager.hpp"
#include "logging/log.hpp"

namespace idhan::api
{

/**
 * @brief Finds a valid cluster name, If the cluster name is already taken, then we'll append a unique identifier to the end of it
 */
drogon::Task< std::string > findValidClusterName( const std::string desired_name, drogon::orm::DbClientPtr db )
{
	const auto result {
		co_await db->execSqlCoro( "SELECT cluster_name FROM file_clusters WHERE cluster_name = $1", desired_name )
	};

	log::info( "Desired name: {}", desired_name );

	if ( result.empty() )
	{
		log::info( "Cluster name {} not in use, Trying it", desired_name );
		co_return desired_name;
	}

	const auto mark_start { desired_name.find_first_of( ":" ) };

	if ( mark_start == std::string::npos )
	{
		co_return co_await findValidClusterName( format_ns::format( "{}: 1", desired_name ), db );
	}

	const auto mark_number_str { desired_name.substr( mark_start + 1 ) };

	try
	{
		const auto mark_number { std::stoull( mark_number_str ) };
		log::info( "Got UID: {}", mark_number_str );
		const auto filtered_name { desired_name.substr( 0, mark_start ) };

		co_return co_await findValidClusterName( format_ns::format( "{}: {}", filtered_name, mark_number + 1 ), db );
	}
	catch ( std::invalid_argument& arg )
	{
		// NOOP
	}

	co_return co_await findValidClusterName( format_ns::format( "{}: 1", desired_name ), db );
}

ClusterAPI::ResponseTask ClusterAPI::add( drogon::HttpRequestPtr request )
{
	auto db { drogon::app().getDbClient() };

	auto transaction { co_await db->newTransactionCoro() };

	const auto& request_json_ptr { request->getJsonObject() };

	if ( request_json_ptr == nullptr ) co_return createBadRequest( "No json data supplied" );

	const auto& request_json { *request_json_ptr };

	std::filesystem::path target_path { request_json[ "path" ].asString() };
	if ( target_path.is_relative() ) target_path = std::filesystem::absolute( target_path );

	const bool readonly { request_json[ "readonly" ].asBool() };

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
			const auto cluster_id { row[ 0 ].as< ClusterID >() };
			const auto cluster_path { row[ 1 ].as< std::string >() };

			const std::filesystem::path path { cluster_path };
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

	std::string fixed_cluster_name { co_await findValidClusterName( cluster_name, db ) };

	log::info( "Creating cluster {} -> {}", cluster_name, fixed_cluster_name );

	bool success { false };

	while ( !success )
	{
		try
		{
			// insert the data
			const auto insert_result { co_await transaction->execSqlCoro(
				"INSERT INTO file_clusters ( cluster_name, folder_path ) VALUES ($1, $2) RETURNING cluster_id",
				fixed_cluster_name,
				target_path.string() ) };

			success = true;

			if ( insert_result.empty() )
			{
				transaction->rollback();

				co_return createInternalError( "Failed to insert new cluster into table" );
			}

			// Set the request cluster_name to match the new name
			auto& json_object_m { *request->getJsonObject() };
			json_object_m[ "name" ] = fixed_cluster_name;

			const auto cluster_id { insert_result[ 0 ][ 0 ].as< ClusterID >() };

			log::debug( "Setting cluster info" );

			// Modify will return `{cluster_id}/list` if it succeeds.
			const auto ret { co_await modifyT( request, cluster_id, transaction ) };

			//TODO: Queue orphan check here.
			co_await filesystem::ClusterManager::getInstance().reloadClusters( db );

			co_return ret;
		}
		catch ( drogon::orm::UniqueViolation& e )
		{
			// NOOP
		}

		fixed_cluster_name = co_await findValidClusterName( cluster_name, db );
	}
}

} // namespace idhan::api