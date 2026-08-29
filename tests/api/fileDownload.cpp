#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>

#include "MimeIDs.hpp"
#include "ServerFixture.hpp"

namespace idhan::test
{

class TemporaryCluster
{
	pqxx::connection& m_connection;
	std::filesystem::path m_path;
	ClusterID m_cluster_id {};

  public:

	TemporaryCluster( pqxx::connection& connection, std::filesystem::path path ) :
	  m_connection( connection ),
	  m_path( std::move( path ) )
	{
		std::filesystem::remove_all( m_path );
	}

	TemporaryCluster( const TemporaryCluster& ) = delete;
	TemporaryCluster& operator=( const TemporaryCluster& ) = delete;

	~TemporaryCluster()
	{
		std::error_code filesystem_error {};
		std::filesystem::remove_all( m_path, filesystem_error );
		if ( filesystem_error ) WARN( "Failed to remove temporary cluster directory: " << filesystem_error.message() );

		if ( m_cluster_id == ClusterID {} ) return;

		try
		{
			pqxx::work tx { m_connection };
			tx.exec( "DELETE FROM file_info WHERE cluster_id = $1", pqxx::params { m_cluster_id } );
			tx.exec( "DELETE FROM file_clusters WHERE cluster_id = $1", pqxx::params { m_cluster_id } );
			tx.commit();
		}
		catch ( const std::exception& e )
		{
			WARN( "Failed to clean up temporary cluster: " << e.what() );
		}
	}

	[[nodiscard]] const std::filesystem::path& path() const { return m_path; }

	void setID( const ClusterID cluster_id ) { m_cluster_id = cluster_id; }
};

static std::string hashOf( pqxx::connection& connection, const RecordID record_id )
{
	pqxx::work tx { connection };
	return tx.query_value< std::string >(
		"SELECT encode(sha256, 'hex') FROM records WHERE record_id = $1", pqxx::params { record_id } );
}

//! Writes \p contents where the server expects the record's file to be, and registers the cluster
//! holding it. The layout is the one getRecordPath builds: <cluster>/f<first two hex>/<hex>.<ext>.
static void storeFile(
	pqxx::connection& connection,
	const RecordID record_id,
	const std::string& hex,
	TemporaryCluster& cluster,
	const std::string& contents,
	const std::string& extension = "png" )
{
	const auto folder { cluster.path() / std::format( "f{}", hex.substr( 0, 2 ) ) };
	std::filesystem::create_directories( folder );

	{
		std::ofstream file { folder / std::format( "{}.{}", hex, extension ), std::ios::binary };
		file << contents;
	}

	pqxx::work tx { connection };
	const auto cluster_id { tx.query_value< ClusterID >(
		"INSERT INTO file_clusters (folder_path) VALUES ($1) RETURNING cluster_id",
		pqxx::params { cluster.path().string() } ) };
	tx.exec(
		"INSERT INTO file_info (record_id, size, mime_id, extension, cluster_id, cluster_store_time) "
		"VALUES ($1, $2, $3, $4, $5, now())",
		pqxx::params { record_id, contents.size(), mime_ids::IMAGE_PNG, extension, cluster_id } );
	tx.commit();
	cluster.setID( cluster_id );
}

SCENARIO_METHOD( ServerFixture, "Fetched file names cannot inject response headers", "[api][file][security]" )
{
	const auto record_id { api().createRecord( 505 ) };
	const auto hex { hashOf( db(), record_id ) };
	const std::string contents { "header safety" };
	const std::string extension { "bad\"\r\nX-Injected: yes" };
	TemporaryCluster cluster {
		db(), std::filesystem::temp_directory_path() / std::format( "idhan-header-safety-test-{}", record_id )
	};
	storeFile( db(), record_id, hex, cluster, contents, extension );

	const auto inline_response { api().get( std::format( "/records/{}/file", record_id ) ) };
	const auto download_response { api().get( std::format( "/records/{}/file?download=true", record_id ) ) };

	CHECK(
		inline_response.header( "content-disposition" )
		== std::format( "inline; filename=\"{}.bad___X-Injected__yes\"", hex ) );
	CHECK(
		download_response.header( "content-disposition" )
		== std::format( "attachment; filename=\"{}.bad___X-Injected__yes\"", hex ) );
	CHECK( inline_response.header( "x-injected" ).empty() );
	CHECK( download_response.header( "x-injected" ).empty() );
}

static void waitForClusterScan( ApiClient& api, const ClusterID cluster_id )
{
	const auto start_response { api.postWithoutBody(
		std::format( "/clusters/{}/scan", cluster_id ), { { "scan_mime", "false" }, { "scan_metadata", "false" } } ) };

	if ( start_response.status != drogon::k200OK || !start_response.json[ "job_id" ].isIntegral() )
		throw std::runtime_error( std::format( "Could not start cluster scan: {}", start_response.body ) );

	const auto job_id { start_response.json[ "job_id" ].asUInt64() };
	const auto deadline { std::chrono::steady_clock::now() + std::chrono::seconds( 10 ) };

	while ( std::chrono::steady_clock::now() < deadline )
	{
		const auto status_response { api.get( std::format( "/jobs/{}/status", job_id ) ) };
		if ( status_response.status != drogon::k200OK )
			throw std::runtime_error( std::format( "Could not read cluster scan status: {}", status_response.body ) );

		if ( status_response.json[ "completed" ].asBool() )
		{
			if ( status_response.json[ "status" ].asString() == "failed" )
				throw std::runtime_error(
					std::format( "Cluster scan failed: {}", status_response.json[ "error" ].asString() ) );

			const auto& response { status_response.json[ "response" ] };
			if ( !response.isString() || response.asString() != "completed" )
				throw std::runtime_error( std::format( "Cluster scan failed: {}", status_response.body ) );

			return;
		}

		std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
	}

	throw std::runtime_error( std::format( "Cluster scan job {} timed out", job_id ) );
}

SCENARIO_METHOD( ServerFixture, "A fetched file is named after its hash", "[api][file]" )
{
	const auto record_id { api().createRecord( 501 ) };
	const auto hex { hashOf( db(), record_id ) };
	const std::string contents { "not really a png, but the bytes do not matter here" };

	TemporaryCluster cluster {
		db(), std::filesystem::temp_directory_path() / std::format( "idhan-file-test-{}", record_id )
	};
	storeFile( db(), record_id, hex, cluster, contents );

	const auto response { api().get( std::format( "/records/{}/file", record_id ) ) };

	CHECK( response.status == drogon::k200OK );
	CHECK( response.body == contents );
	// Without this a browser saves every record in the collection as "file", after the URL's last segment.
	CHECK( response.header( "content-disposition" ) == std::format( "inline; filename=\"{}.png\"", hex ) );
}

SCENARIO_METHOD( ServerFixture, "A cluster scan records first detection time", "[api][cluster][scan]" )
{
	const auto record_id { api().createRecord( 502 ) };
	const auto hex { hashOf( db(), record_id ) };
	TemporaryCluster cluster {
		db(), std::filesystem::temp_directory_path() / std::format( "idhan-cluster-scan-test-{}", record_id )
	};

	const auto folder { cluster.path() / std::format( "f{}", hex.substr( 0, 2 ) ) };
	std::filesystem::create_directories( folder );
	const std::string contents { "present in the scanned cluster" };
	{
		std::ofstream file { folder / std::format( "{}.png", hex ), std::ios::binary };
		file << contents;
	}

	ClusterID cluster_id {};
	std::string before_scan {};
	{
		pqxx::work tx { db() };
		cluster_id = tx.query_value< ClusterID >(
			"INSERT INTO file_clusters (folder_path) VALUES ($1) RETURNING cluster_id",
			pqxx::params { cluster.path().string() } );
		cluster.setID( cluster_id );
		tx.exec(
			"INSERT INTO file_info (record_id, size, mime_id, cluster_id, cluster_store_time) "
			"VALUES ($1, $2, $3, $4, NULL)",
			pqxx::params { record_id, contents.size(), mime_ids::IMAGE_PNG, cluster_id } );

		const auto initial_time {
			tx.exec( "SELECT cluster_store_time FROM file_info WHERE record_id = $1", pqxx::params { record_id } )
		};
		REQUIRE( initial_time[ 0 ][ 0 ].is_null() );
		before_scan = tx.query_value< std::string >( "SELECT clock_timestamp()::timestamp::text" );
		tx.commit();
	}

	waitForClusterScan( api(), cluster_id );

	std::string first_detection_time {};
	{
		pqxx::work tx { db() };
		const auto stored_time { tx.exec(
			"SELECT cluster_store_time::text, cluster_store_time >= $2::timestamp "
			"FROM file_info WHERE record_id = $1",
			pqxx::params { record_id, before_scan } ) };
		REQUIRE_FALSE( stored_time[ 0 ][ 0 ].is_null() );
		CHECK( stored_time[ 0 ][ 1 ].as< bool >() );
		first_detection_time = stored_time[ 0 ][ 0 ].as< std::string >();
	}

	waitForClusterScan( api(), cluster_id );

	pqxx::work tx { db() };
	const auto second_detection_time { tx.query_value< std::string >(
		"SELECT cluster_store_time::text FROM file_info WHERE record_id = $1", pqxx::params { record_id } ) };
	CHECK( second_detection_time == first_detection_time );
}

} // namespace idhan::test
