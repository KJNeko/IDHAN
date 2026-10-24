#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
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
	ApiClient* m_api {};
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
			tx.commit();
			if ( m_api ) CHECK( m_api->del( std::format( "/clusters/{}/remove", m_cluster_id ) ).status == 200 );
		}
		catch ( const std::exception& e )
		{
			WARN( "Failed to clean up temporary cluster: " << e.what() );
		}
	}

	[[nodiscard]] const std::filesystem::path& path() const { return m_path; }

	ClusterID registerWith( ApiClient& api )
	{
		m_api = &api;
		std::filesystem::create_directories( m_path );
		Json::Value body;
		body[ "name" ] = m_path.filename().string();
		body[ "path" ] = m_path.string();
		body[ "readonly" ] = true;
		const auto response { api.post( "/clusters/add", body ) };
		if ( response.status != 201 ) throw std::runtime_error( "Cannot register temporary cluster: " + response.body );
		m_cluster_id = static_cast< ClusterID >( response.json[ "cluster_id" ].asInt() );
		return m_cluster_id;
	}
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
	ApiClient& api,
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

	const auto cluster_id { cluster.registerWith( api ) };
	pqxx::work tx { connection };
	tx.exec(
		"INSERT INTO file_info (record_id, size, mime_id, extension, cluster_id, cluster_store_time) "
		"VALUES ($1, $2, $3, $4, $5, now())",
		pqxx::params { record_id, contents.size(), mime_ids::IMAGE_PNG, extension, cluster_id } );
	tx.commit();
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
	storeFile( api(), db(), record_id, hex, cluster, contents, extension );

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

static Json::Value waitForClusterScan(
	ApiClient& api,
	const ClusterID cluster_id,
	QueryParams query = { { "scan_mime", "false" }, { "scan_metadata", "false" } } )
{
	const auto start_response { api.postWithoutBody( std::format( "/clusters/{}/scan", cluster_id ), query ) };

	if ( start_response.status != 200 || !start_response.json[ "job_id" ].isIntegral() )
		throw std::runtime_error( std::format( "Could not start cluster scan: {}", start_response.body ) );

	const auto job_id { start_response.json[ "job_id" ].asUInt64() };
	const auto deadline { std::chrono::steady_clock::now() + std::chrono::seconds( 10 ) };

	while ( std::chrono::steady_clock::now() < deadline )
	{
		const auto status_response { api.get( std::format( "/jobs/{}/status", job_id ) ) };
		if ( status_response.status != 200 )
			throw std::runtime_error( std::format( "Could not read cluster scan status: {}", status_response.body ) );

		if ( status_response.json[ "completed" ].asBool() )
		{
			if ( status_response.json[ "status" ].asString() == "failed" )
				throw std::runtime_error(
					std::format( "Cluster scan failed: {}", status_response.json[ "error" ].asString() ) );

			const auto& response { status_response.json[ "response" ] };
			if ( !response.isObject() || !response[ "failed_count" ].isIntegral() )
				throw std::runtime_error( std::format( "Cluster scan failed: {}", status_response.body ) );

			return response;
		}

		std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
	}

	throw std::runtime_error( std::format( "Cluster scan job {} timed out", job_id ) );
}

SCENARIO_METHOD( ServerFixture, "Cluster scan usage follows registered files", "[api][cluster][scan][usage]" )
{
	const auto record_id { api().createRecord( 506 ) };
	const auto hex { hashOf( db(), record_id ) };
	const std::string contents { "registered file bytes" };
	TemporaryCluster cluster {
		db(), std::filesystem::temp_directory_path() / std::format( "idhan-cluster-usage-test-{}", record_id )
	};
	storeFile( api(), db(), record_id, hex, cluster, contents );
	const auto file_path { cluster.path() / std::format( "f{}/{}.png", hex.substr( 0, 2 ), hex ) };
	ClusterID cluster_id {};
	{
		pqxx::work tx { db() };
		cluster_id = tx.query_value< ClusterID >(
			"SELECT cluster_id FROM file_info WHERE record_id = $1", pqxx::params { record_id } );
	}

	std::uint64_t expected_count { 1 };
	std::uint64_t expected_size { contents.size() };
	std::uint64_t scanned_count { 1 };
	std::uint64_t scanned_bytes { contents.size() };
	std::uint64_t failed_count {};
	QueryParams query { { "scan_mime", "false" }, { "scan_metadata", "false" } };

	SECTION( "Rejected files only contribute to scan statistics" )
	{
		const std::string rejected { "unregistered bytes" };
		std::ofstream file { file_path.parent_path() / "invalid-filename.png", std::ios::binary };
		file << rejected;
		file.close();
		REQUIRE( file.good() );
		scanned_count = 2;
		scanned_bytes += rejected.size();
		failed_count = 1;
	}

	SECTION( "Registering a discovered file increments usage once" )
	{
		pqxx::work tx { db() };
		tx.exec( "DELETE FROM file_info WHERE record_id = $1", pqxx::params { record_id } );
		tx.commit();
	}

	SECTION( "Updating a registered size uses the trigger delta" )
	{
		pqxx::work tx { db() };
		tx.exec( "UPDATE file_info SET size = 1 WHERE record_id = $1", pqxx::params { record_id } );
		tx.commit();
	}

	SECTION( "Detaching a missing file decrements usage once" )
	{
		REQUIRE( std::filesystem::remove( file_path ) );
		query.emplace_back( "remove_missing_files", "true" );
		expected_count = 0;
		expected_size = 0;
		scanned_count = 0;
		scanned_bytes = 0;
		failed_count = 1;
	}

	const auto result { waitForClusterScan( api(), cluster_id, query ) };
	CHECK( result[ "scanned_count" ].asUInt64() == scanned_count );
	CHECK( result[ "scanned_bytes" ].asUInt64() == scanned_bytes );
	CHECK( result[ "failed_count" ].asUInt64() == failed_count );

	// Repeating the scan must not double-count registrations or missing-file detachment.
	waitForClusterScan( api(), cluster_id, query );
	pqxx::work tx { db() };
	const auto usage {
		tx.exec( "SELECT file_count, size_used FROM file_clusters WHERE cluster_id = $1", pqxx::params { cluster_id } )
	};
	REQUIRE( usage.size() == 1 );
	CHECK( usage[ 0 ][ "file_count" ].as< std::uint64_t >() == expected_count );
	CHECK( usage[ 0 ][ "size_used" ].as< std::uint64_t >() == expected_size );
}

SCENARIO_METHOD(
	ServerFixture,
	"Failed archive metadata writes roll back and can be retried",
	"[api][metadata][archive][transaction]" )
{
	// A stored ZIP containing member.txt with the contents "transaction-contained".
	constexpr char zip[] {
		"\x50\x4b\x03\x04\x14\x00\x00\x00\x00\x00\x00\x00\x21\x50\xac\x97\xed\x82\x15\x00"
		"\x00\x00\x15\x00\x00\x00\x0a\x00\x00\x00\x6d\x65\x6d\x62\x65\x72\x2e\x74\x78\x74"
		"\x74\x72\x61\x6e\x73\x61\x63\x74\x69\x6f\x6e\x2d\x63\x6f\x6e\x74\x61\x69\x6e\x65"
		"\x64\x50\x4b\x01\x02\x14\x03\x14\x00\x00\x00\x00\x00\x00\x00\x21\x50\xac\x97\xed"
		"\x82\x15\x00\x00\x00\x15\x00\x00\x00\x0a\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
		"\x00\x80\x01\x00\x00\x00\x00\x6d\x65\x6d\x62\x65\x72\x2e\x74\x78\x74\x50\x4b\x05"
		"\x06\x00\x00\x00\x00\x01\x00\x01\x00\x38\x00\x00\x00\x3d\x00\x00\x00\x00\x00"
	};
	const auto record_id { api().createRecord( 507 ) };
	const auto hex { hashOf( db(), record_id ) };
	TemporaryCluster cluster {
		db(), std::filesystem::temp_directory_path() / std::format( "idhan-metadata-rollback-test-{}", record_id )
	};
	storeFile( api(), db(), record_id, hex, cluster, std::string { zip, sizeof( zip ) - 1 }, "zip" );
	std::int64_t initial_records {};
	std::int64_t initial_archives {};
	std::int64_t initial_mappings {};
	{
		pqxx::work tx { db() };
		tx.exec(
			"UPDATE file_info SET mime_id = $2 WHERE record_id = $1",
			pqxx::params { record_id, mime_ids::APPLICATION_ZIP } );
		initial_records = tx.query_value< std::int64_t >( "SELECT count(*) FROM records" );
		initial_archives = tx.query_value< std::int64_t >( "SELECT count(*) FROM archives" );
		initial_mappings = tx.query_value< std::int64_t >( "SELECT count(*) FROM archive_map" );
		tx.exec(
			"CREATE FUNCTION reject_test_archive_metadata() RETURNS trigger LANGUAGE plpgsql AS $$ "
			"BEGIN RAISE EXCEPTION 'injected archive metadata failure'; END $$" );
		tx.exec(
			"CREATE TRIGGER reject_test_archive_metadata BEFORE INSERT ON archive_metadata "
			"FOR EACH ROW EXECUTE FUNCTION reject_test_archive_metadata()" );
		tx.commit();
	}

	const auto failed { api().postWithoutBody( std::format( "/records/{}/metadata/scan", record_id ) ) };
	CHECK( failed.status == 500 );
	{
		pqxx::work tx { db() };
		CHECK_FALSE( tx.query_value< bool >(
			"SELECT EXISTS (SELECT 1 FROM metadata WHERE record_id = $1)", pqxx::params { record_id } ) );
		CHECK( tx.query_value< std::int64_t >( "SELECT count(*) FROM records" ) == initial_records );
		CHECK( tx.query_value< std::int64_t >( "SELECT count(*) FROM archives" ) == initial_archives );
		CHECK( tx.query_value< std::int64_t >( "SELECT count(*) FROM archive_map" ) == initial_mappings );
		CHECK( tx.query_value< bool >(
			"SELECT file_mtime IS NULL FROM file_info WHERE record_id = $1", pqxx::params { record_id } ) );
		tx.exec( "DROP TRIGGER reject_test_archive_metadata ON archive_metadata" );
		tx.exec( "DROP FUNCTION reject_test_archive_metadata()" );
		tx.commit();
	}

	const auto retried { api().postWithoutBody( std::format( "/records/{}/metadata/scan", record_id ) ) };
	REQUIRE( retried.status == 200 );
	pqxx::work tx { db() };
	CHECK( tx.query_value< bool >(
		"SELECT EXISTS (SELECT 1 FROM metadata WHERE record_id = $1)", pqxx::params { record_id } ) );
	CHECK( tx.query_value< std::int64_t >( "SELECT count(*) FROM records" ) == initial_records + 1 );
	CHECK( tx.query_value< std::int64_t >( "SELECT count(*) FROM archives" ) == initial_archives + 1 );
	CHECK(
		tx.query_value< std::int64_t >(
			"SELECT count(*) FROM archive_map JOIN archive_metadata USING (archive_id) "
			"WHERE archive_metadata.record_id = $1 AND path = 'member.txt'",
			pqxx::params { record_id } )
		== 1 );
	CHECK( tx.query_value< bool >(
		"SELECT file_mtime IS NOT NULL FROM file_info WHERE record_id = $1", pqxx::params { record_id } ) );
}

SCENARIO_METHOD( ServerFixture, "A fetched file is named after its hash", "[api][file]" )
{
	const auto record_id { api().createRecord( 501 ) };
	const auto hex { hashOf( db(), record_id ) };
	const std::string contents { "not really a png, but the bytes do not matter here" };

	TemporaryCluster cluster {
		db(), std::filesystem::temp_directory_path() / std::format( "idhan-file-test-{}", record_id )
	};
	storeFile( api(), db(), record_id, hex, cluster, contents );

	SECTION( "Extension without a leading dot" )
	{}
	SECTION( "Extension with a leading dot" )
	{
		pqxx::work tx { db() };
		tx.exec( "UPDATE file_info SET extension = '.png' WHERE record_id = $1", pqxx::params { record_id } );
		tx.commit();
	}

	const auto response { api().get( std::format( "/records/{}/file", record_id ) ) };

	CHECK( response.status == 200 );
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

	const auto cluster_id { cluster.registerWith( api() ) };
	std::string before_scan {};
	{
		pqxx::work tx { db() };
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
