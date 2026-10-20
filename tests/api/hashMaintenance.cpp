#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <format>
#include <thread>

#include "MimeIDs.hpp"
#include "ServerFixture.hpp"

namespace idhan::test
{

class BackfillCluster
{
	pqxx::connection& m_connection;
	std::filesystem::path m_path;
	ClusterID m_id {};

  public:

	BackfillCluster( ApiClient& api, pqxx::connection& connection ) :
	  m_connection( connection ),
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
			transaction.exec( "DELETE FROM file_clusters WHERE cluster_id = $1", pqxx::params { m_id } );
			transaction.commit();
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
	cluster.hideMarker();
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
	std::this_thread::sleep_for( std::chrono::milliseconds { 1200 } );
	CHECK_FALSE( hasBackfilledHash( db(), valid ) );
	CHECK( backfillMetadataSignature( db(), valid ) == signature );
	cluster.restoreMarker();
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
}

} // namespace idhan::test
