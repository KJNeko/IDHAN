#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <format>
#include <fstream>

#include "ServerFixture.hpp"

namespace idhan::test
{

SCENARIO_METHOD( ServerFixture, "Removing a cluster leaves its files on disk", "[api][cluster][remove]" )
{
	const auto record_id { api().createRecord( 901 ) };
	const auto cluster_path {
		std::filesystem::temp_directory_path() / std::format( "idhan-remove-cluster-test-{}", record_id )
	};
	std::filesystem::create_directories( cluster_path );
	const auto sentinel_path { cluster_path / "must-remain.bin" };
	{
		std::ofstream sentinel { sentinel_path, std::ios::binary };
		sentinel << "leave this file alone";
	}

	pqxx::work setup { db() };
	const auto cluster_id { setup.query_value< ClusterID >(
		"INSERT INTO file_clusters (cluster_name, folder_path) VALUES ('removable', $1) RETURNING cluster_id",
		pqxx::params { cluster_path.string() } ) };
	setup.exec(
		"INSERT INTO file_info (record_id, size, extension, cluster_id, cluster_store_time) "
		"VALUES ($1, 21, 'bin', $2, now())",
		pqxx::params { record_id, cluster_id } );
	setup.commit();

	const auto response { api().del( std::format( "/clusters/{}/remove", cluster_id ) ) };

	REQUIRE( response.status == drogon::k200OK );
	CHECK( std::filesystem::exists( sentinel_path ) );

	pqxx::work verify { db() };
	CHECK_FALSE( verify.query_value< bool >(
		"SELECT EXISTS (SELECT 1 FROM file_clusters WHERE cluster_id = $1)", pqxx::params { cluster_id } ) );
	const auto file_info { verify.exec(
		"SELECT cluster_id, cluster_delete_time FROM file_info WHERE record_id = $1", pqxx::params { record_id } ) };
	REQUIRE( file_info.size() == 1 );
	CHECK( file_info[ 0 ][ "cluster_id" ].is_null() );
	CHECK( file_info[ 0 ][ "cluster_delete_time" ].is_null() );
	verify.commit();

	const auto missing_response { api().del( std::format( "/clusters/{}/remove", cluster_id ) ) };
	CHECK( missing_response.status == drogon::k404NotFound );

	std::filesystem::remove_all( cluster_path );
}

} // namespace idhan::test
