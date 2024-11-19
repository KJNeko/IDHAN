//
// Created by kj16609 on 11/18/24.
//

#include <fstream>

#include "api/ClusterAPI.hpp"
#include "exceptions.hpp"
#include "logging/log.hpp"

namespace idhan::api
{

ClusterAPI::ResponseTask ClusterAPI::add( drogon::HttpRequestPtr request )
{
	auto db { drogon::app().getDbClient() };

	auto transaction { co_await db->newTransactionCoro() };

	const auto request_json_ptr { request->getJsonObject() };

	if ( request_json_ptr == nullptr ) throw InvalidRequestBody();

	const auto& request_json { *request_json_ptr };

	std::filesystem::path target_path { request_json[ "path" ].asString() };

	// Change the target path to be absolute
	if ( target_path.is_relative() ) target_path = std::filesystem::absolute( target_path );

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
				//TODO: Exception
				transaction->rollback();
				throw std::runtime_error( "Path already exists" );
			}
		}
	}

	log::debug( "Found no conflicting paths" );

	// The path was not already in use. Now check if it's valid
	if ( !std::filesystem::exists( target_path ) )
	{
		if ( !std::filesystem::create_directory( target_path ) )
		{
			transaction->rollback();
			throw std::runtime_error( "Failed to create directory " + target_path.string() );
		}
	}

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
		throw std::runtime_error( "Failed to write to file " + target_path.string() );
	}

	log::debug( "Write test passed. Inserting new cluster into table" );

	// delete the test
	std::filesystem::remove( target_path / "write_test.txt" );

	const std::string folder_name { request_json[ "name" ].isString() ? request_json[ "name" ].asString() :
		                                                                "Cluster Temp Name" };

	// insert the data
	const auto insert_result { co_await transaction->execSqlCoro(
		"INSERT INTO file_clusters ( cluster_name, folder_path ) VALUES ($1, $2) RETURNING cluster_id",
		folder_name,
		target_path.string() ) };

	if ( insert_result.empty() )
	{
		transaction->rollback();
		throw std::runtime_error( "Failed to insert into file_clusters" );
	}

	const auto cluster_id { insert_result[ 0 ][ 0 ].as< ClusterID >() };

	log::debug( "Setting cluster info" );

	// Modify will return `{cluster_id}/list` if it succeeds.
	co_return co_await modifyT( request, cluster_id, transaction );
}

} // namespace idhan::api