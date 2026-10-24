#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <format>
#include <map>
#include <sstream>
#include <thread>

#include "MimeIDs.hpp"
#include "ServerFixture.hpp"

namespace idhan::test
{

static std::map< std::string, Json::Value > backgroundJobs( ApiClient& api )
{
	const auto response { api.get( "/jobs/status" ) };
	REQUIRE( response.status == 200 );
	std::map< std::string, Json::Value > jobs;
	for ( const auto& job : response.json[ "jobs" ] )
	{
		const auto& progress { job[ "progress" ] };
		if ( progress[ "kind" ].asString() != "background_service" ) continue;

		const auto service { progress[ "service" ].asString() };
		REQUIRE_FALSE( jobs.contains( service ) );
		jobs.emplace( service, job );
	}
	return jobs;
}

static std::map< std::string, Json::Value > awaitBackgroundJobs( ApiClient& api, const bool include_backfill = true )
{
	const auto deadline { std::chrono::steady_clock::now() + std::chrono::seconds { 5 } };
	while ( std::chrono::steady_clock::now() < deadline )
	{
		auto jobs { backgroundJobs( api ) };
		if ( jobs.contains( "hamming_distance" ) && jobs.contains( "module_workers" )
		     && ( !include_backfill || jobs.contains( "phash_backfill" ) ) )
			return jobs;

		std::this_thread::sleep_for( std::chrono::milliseconds { 20 } );
	}
	FAIL( "Startup services did not register their background jobs" );
	return {};
}

TEST_CASE_METHOD(
	ServerFixture,
	"Startup background services stay visible while waiting and survive status cleanup",
	"[api][maintenance][background-jobs]" )
{
	const auto jobs { awaitBackgroundJobs( api(), false ) };
	CHECK_FALSE( jobs.contains( "heartbeat" ) );
	CHECK_FALSE( jobs.contains( "downloader" ) );
	for ( const auto service : { "hamming_distance", "module_workers" } )
	{
		REQUIRE( jobs.contains( service ) );
		const auto& job { jobs.at( service ) };
		CHECK( job[ "status" ].asString() == "running" );
		CHECK_FALSE( job[ "completed" ].asBool() );
		REQUIRE( job[ "continuous" ].isBool() );
		CHECK( job[ "continuous" ].asBool() );
		CHECK_FALSE( job[ "progress" ][ "phase" ].asString().empty() );
		CHECK_FALSE( job[ "progress" ][ "message" ].asString().empty() );
		CHECK( job[ "progress" ][ "updated_at" ].asInt64() > 0 );
		CHECK_FALSE( job[ "progress" ].isMember( "completed_cycles" ) );
		const auto individual { api().get( std::format( "/jobs/{}/status", job[ "job_id" ].as< JobID >() ) ) };
		REQUIRE( individual.status == 200 );
		CHECK( individual.json[ "continuous" ].asBool() );
		CHECK( individual.json[ "progress" ][ "service" ].asString() == service );
	}
	CHECK( api().get( "/jobs/status", { { "cleanup", "true" } } ).status == 200 );
	const auto after_cleanup { backgroundJobs( api() ) };
	for ( const auto service : { "hamming_distance", "module_workers" } )
	{
		REQUIRE( after_cleanup.contains( service ) );
		CHECK( after_cleanup.at( service )[ "job_id" ] == jobs.at( service )[ "job_id" ] );
		CHECK_FALSE( after_cleanup.at( service )[ "completed" ].asBool() );
	}

	const auto updated_at { jobs.at( "hamming_distance" )[ "progress" ][ "updated_at" ].asInt64() };
	const auto deadline { std::chrono::steady_clock::now() + std::chrono::seconds { 5 } };
	bool advanced {};
	while ( std::chrono::steady_clock::now() < deadline )
	{
		const auto current { backgroundJobs( api() ).at( "hamming_distance" ) };
		advanced = current[ "progress" ][ "updated_at" ].asInt64() > updated_at;
		if ( advanced ) break;

		std::this_thread::sleep_for( std::chrono::milliseconds { 20 } );
	}
	CHECK( advanced );
}

class BackfillCluster
{
	pqxx::connection& m_connection;
	ApiClient& m_api;
	std::filesystem::path m_path;
	ClusterID m_id {};

  public:

	BackfillCluster( ApiClient& api, pqxx::connection& connection ) :
	  m_connection( connection ),
	  m_api( api ),
	  m_path( std::filesystem::temp_directory_path() / std::format( "idhan-backfill-{}", testServer().port() ) )
	{
		std::filesystem::remove_all( m_path );
		std::filesystem::create_directories( m_path );
		Json::Value body;
		body[ "name" ] = "hash-backfill-test";
		body[ "path" ] = m_path.string();
		body[ "readonly" ] = true;
		const auto response { api.post( "/clusters/add", body ) };
		if ( response.status != 201 ) throw std::runtime_error( "Cannot register backfill cluster: " + response.body );
		m_id = static_cast< ClusterID >( response.json[ "cluster_id" ].asInt() );
	}

	~BackfillCluster()
	{
		try
		{
			pqxx::work transaction { m_connection };
			transaction.exec( "DELETE FROM file_info WHERE cluster_id = $1", pqxx::params { m_id } );
			transaction.commit();
			CHECK( m_api.del( std::format( "/clusters/{}/remove", m_id ) ).status == 200 );
			std::filesystem::remove_all( m_path );
		}
		catch ( const std::exception& error )
		{
			WARN( "Backfill cluster cleanup: " << error.what() );
		}
	}

	void hideMarker() { std::filesystem::rename( m_path / ".idhan-cluster.json", m_path / "marker.saved" ); }

	void restoreMarker() { std::filesystem::rename( m_path / "marker.saved", m_path / ".idhan-cluster.json" ); }

	void seed( const RecordID id, const bool with_file )
	{
		pqxx::work transaction { m_connection };
		const auto hash { transaction.query_value< std::string >(
			"SELECT encode(sha256, 'hex') FROM records WHERE record_id = $1", pqxx::params { id } ) };
		if ( with_file )
		{
			const auto directory { m_path / ( "f" + hash.substr( 0, 2 ) ) };
			std::filesystem::create_directories( directory );
			std::filesystem::copy_file(
				IDHAN_SOURCE_DIR "/IDHANServer/src/static/Titorv2.png", directory / ( hash + ".png" ) );
		}
		transaction.exec(
			"INSERT INTO file_info (record_id, size, mime_id, extension, cluster_id, cluster_store_time, file_mtime) "
			"VALUES ($1, 987, $2, 'png', $3, to_timestamp(123456), to_timestamp(123456))",
			pqxx::params { id, mime_ids::IMAGE_PNG, m_id } );
		transaction.exec(
			"INSERT INTO image_metadata (record_id, width, height, channels, has_exif) VALUES ($1, 123, 456, 3, true)",
			pqxx::params { id } );
		transaction.commit();
	}
};

static std::string backfillMetadataSignature( pqxx::connection& connection, const RecordID id )
{
	pqxx::nontransaction transaction { connection };
	return transaction.query_value< std::string >(
		"SELECT ((to_jsonb(i) - 'phash') || to_jsonb(f))::text "
		"FROM image_metadata i JOIN file_info f USING(record_id) WHERE i.record_id = $1",
		pqxx::params { id } );
}

static bool hasBackfilledHash( pqxx::connection& connection, const RecordID id )
{
	pqxx::nontransaction transaction { connection };
	return transaction.query_value< bool >(
		"SELECT phash IS NOT NULL FROM image_metadata WHERE record_id = $1", pqxx::params { id } );
}

static bool awaitBackfill( pqxx::connection& connection, const RecordID id )
{
	const auto deadline { std::chrono::steady_clock::now() + std::chrono::seconds { 10 } };
	while ( std::chrono::steady_clock::now() < deadline )
	{
		if ( hasBackfilledHash( connection, id ) ) return true;

		std::this_thread::sleep_for( std::chrono::milliseconds { 50 } );
	}
	return false;
}

TEST_CASE_METHOD(
	ServerFixture,
	"Background phash backfill verifies clusters and advances past unreadable files",
	"[api][maintenance][phash]" )
{
	BackfillCluster cluster { api(), db() };
	std::vector< RecordID > missing;
	for ( int seed { 800 }; seed < 808; ++seed )
	{
		const auto id { api().createRecord( seed ) };
		missing.push_back( id );
		cluster.seed( id, false );
	}
	const auto valid { api().createRecord( 808 ) };
	cluster.seed( valid, true );
	const auto signature { backfillMetadataSignature( db(), valid ) };
	// The backfill now runs once at startup. Hold its table until startup has
	// verified the marker, then remove the marker before allowing the sweep to read.
	testServer().stop();
	{
		pqxx::nontransaction reset { db() };
		reset.exec( "UPDATE image_metadata SET phash = NULL" );
	}
	pqxx::work blocked { db() };
	blocked.exec( "LOCK TABLE image_metadata IN ACCESS EXCLUSIVE MODE" );
	testServer().restart( false );
	cluster.hideMarker();
	blocked.commit();
	Json::Value backfill;
	const auto deadline { std::chrono::steady_clock::now() + std::chrono::seconds { 10 } };
	while ( std::chrono::steady_clock::now() < deadline )
	{
		backfill = awaitBackgroundJobs( api() ).at( "phash_backfill" );
		if ( backfill[ "completed" ].asBool() ) break;
		std::this_thread::sleep_for( std::chrono::milliseconds { 20 } );
	}
	REQUIRE( backfill[ "completed" ].asBool() );
	CHECK_FALSE( backfill[ "continuous" ].asBool() );
	CHECK( backfill[ "progress" ][ "failed_count" ].asUInt64() > 0 );
	CHECK_FALSE( backfill[ "progress" ][ "last_error" ].asString().empty() );
	CHECK_FALSE( hasBackfilledHash( db(), valid ) );
	CHECK( backfillMetadataSignature( db(), valid ) == signature );
	cluster.restoreMarker();
	testServer().restart( false );
	REQUIRE( awaitBackfill( db(), valid ) );
	CHECK( backfillMetadataSignature( db(), valid ) == signature );
	for ( const auto id : missing )
	{
		CHECK_FALSE( hasBackfilledHash( db(), id ) );
		pqxx::nontransaction transaction { db() };
		CHECK( transaction.query_value< bool >(
			"SELECT cluster_id IS NOT NULL FROM file_info WHERE record_id = $1", pqxx::params { id } ) );
	}
}

TEST_CASE_METHOD(
	ServerFixture,
	"Phash backfill uses shared cluster configuration across batches",
	"[api][maintenance][phash][cluster-cache]" )
{
	BackfillCluster cluster { api(), db() };
	std::vector< RecordID > records;
	for ( int seed { 830 }; seed < 846; ++seed ) records.push_back( api().createRecord( seed ) );

	testServer().stop();
	for ( const auto record : records ) cluster.seed( record, true );
	testServer().restart( false );
	for ( const auto record : records ) REQUIRE( awaitBackfill( db(), record ) );

	const auto response { api().get( "/log", { { "level", "debug" } } ) };
	REQUIRE( response.status == 200 );
	std::istringstream logs { response.body };
	std::string line;
	std::size_t cluster_lookups {};
	while ( std::getline( logs, line ) )
	{
		if ( !line.contains( "PostgreSQL query complete:" ) ) continue;

		if ( line.contains( "FROM file_clusters" ) ) ++cluster_lookups;
	}
	// Configuration loads once at startup; both backfill batches reuse it without more cluster queries.
	CHECK( cluster_lookups == 1 );
}

TEST_CASE_METHOD(
	ServerFixture,
	"Hash workers recover existing backlog at startup and stop cleanly",
	"[api][maintenance][phash]" )
{
	BackfillCluster cluster { api(), db() };
	const auto id { api().createRecord( 820 ) };
	testServer().stop();
	cluster.seed( id, true );
	const auto signature { backfillMetadataSignature( db(), id ) };
	{
		pqxx::nontransaction transaction { db() };
		// This record has no file; its pre-existing hash still needs pair maintenance.
		const auto other { transaction.query_value< RecordID >(
			"INSERT INTO records (sha256) VALUES (decode(repeat('ff', 32), 'hex')) RETURNING record_id" ) };
		transaction.exec(
			"INSERT INTO image_metadata (record_id, width, height, channels, phash) VALUES ($1, 1, 1, 3, 'x0000000000000000')",
			pqxx::params { other } );
	}
	testServer().restart( false );
	CHECK( awaitBackgroundJobs( api() ).size() == 3 );
	REQUIRE( awaitBackfill( db(), id ) );
	CHECK( backfillMetadataSignature( db(), id ) == signature );
	const auto deadline { std::chrono::steady_clock::now() + std::chrono::seconds { 10 } };
	bool drained {};
	while ( !drained && std::chrono::steady_clock::now() < deadline )
	{
		pqxx::nontransaction transaction { db() };
		drained = transaction.query_value< bool >( "SELECT NOT EXISTS(SELECT FROM hamming_distance_queue)" );
		if ( !drained ) std::this_thread::sleep_for( std::chrono::milliseconds { 50 } );
	}
	CHECK( drained );
	const auto stopping { std::chrono::steady_clock::now() };
	testServer().stop();
	CHECK( std::chrono::steady_clock::now() - stopping < std::chrono::seconds { 5 } );
	testServer().restart( false );
	CHECK( api().get( "/health" ).status == 200 );
	CHECK( awaitBackgroundJobs( api() ).size() == 3 );
}

} // namespace idhan::test
