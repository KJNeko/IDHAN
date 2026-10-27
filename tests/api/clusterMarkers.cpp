#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <catch2/catch_test_macros.hpp>
#include <json/reader.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <span>
#include <sstream>
#include <thread>

#include "MimeIDs.hpp"
#include "JobEventStream.hpp"
#include "ServerFixture.hpp"
#include "filesystem/PathHelpers.hpp"
#include "idhan/SHA256.hpp"

namespace idhan::test
{

class ClusterMarkerFixture : public ServerFixture
{
  public:

	const std::string name { "marker-" + boost::uuids::to_string( boost::uuids::random_generator {}() ) };
	const std::filesystem::path directory { std::filesystem::temp_directory_path() / name };
	const std::filesystem::path root { directory / "root" };
	ClusterID cluster_id {};

	ClusterMarkerFixture() { std::filesystem::create_directories( root ); }

	~ClusterMarkerFixture()
	{
		if ( cluster_id != 0 )
		{
			try
			{
				(void)api().del( std::format( "/clusters/{}/remove", cluster_id ) );
			}
			catch ( const std::exception& error )
			{
				WARN( error.what() );
			}
		}
		std::error_code error;
		std::filesystem::permissions(
			root, std::filesystem::perms::owner_all, std::filesystem::perm_options::add, error );
		std::filesystem::remove_all( directory, error );
	}

	Json::Value body( const std::filesystem::path& path, const std::string& suffix = "", const bool readonly = false )
		const
	{
		Json::Value value { Json::objectValue };
		value[ "name" ] = name + suffix;
		value[ "path" ] = path.string();
		value[ "readonly" ] = readonly;
		return value;
	}

	void create( const bool readonly = false )
	{
		const auto response { api().post( "/clusters/add", body( root, "", readonly ) ) };
		REQUIRE( response.status == 201 );
		cluster_id = static_cast< ClusterID >( response.json[ "cluster_id" ].asInt() );
	}

	Json::Value marker() const
	{
		std::ifstream file { root / ".idhan-cluster.json" };
		Json::Value value;
		file >> value;
		return value;
	}

	void writeMarker( const Json::Value& document ) const
	{
		std::ofstream file { root / ".idhan-cluster.json" };
		file << document;
	}

	Json::Value scanStatus( const QueryParams& query = {} ) const
	{
		const auto queued { api().postWithoutBody( std::format( "/clusters/{}/scan", cluster_id ), query ) };
		REQUIRE( queued.status == 200 );
		const auto job_id { queued.json[ "job_id" ].as< JobID >() };
		const auto deadline { std::chrono::steady_clock::now() + std::chrono::seconds( 15 ) };
		while ( std::chrono::steady_clock::now() < deadline )
		{
			const auto status { api().get( std::format( "/jobs/{}/status", job_id ) ) };
			REQUIRE( status.status == 200 );
			if ( status.json[ "completed" ].asBool() )
			{
				REQUIRE( status.json[ "status" ].asString() == "completed" );
				return status.json;
			}
			std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
		}
		FAIL( "Cluster scan did not finish" );
		return {};
	}

	Json::Value scan( const QueryParams& query = {} ) const { return scanStatus( query )[ "response" ]; }

	std::filesystem::path orphan( const std::string& bytes, const bool canonical ) const
	{
		const auto hash { SHA256::hash( std::as_bytes( std::span { bytes.data(), bytes.size() } ) ) };
		const auto relative { filesystem::getClusterRelativePath( hash, "bin" ) };
		const auto path { canonical ? root / relative : root / "misplaced" / relative.filename() };
		std::filesystem::create_directories( path.parent_path() );
		std::ofstream file { path, std::ios::binary };
		file << bytes;
		return path;
	}
};

TEST_CASE_METHOD(
	ClusterMarkerFixture,
	"Cluster markers bind identity and preserve extensible metadata",
	"[api][cluster-markers]" )
{
	create();
	auto document { marker() };
	CHECK( document[ "format" ].asString() == "idhan.cluster" );
	CHECK( document[ "version" ].asInt() == 1 );
	CHECK( document[ "metadata" ].isObject() );
	{
		pqxx::work transaction { db() };
		CHECK(
			transaction.query_value< std::string >(
				"SELECT cluster_uuid::text FROM file_clusters WHERE cluster_id = $1", pqxx::params { cluster_id } )
			== document[ "cluster_uuid" ].asString() );
	}
	document[ "metadata" ][ "future" ] = "keep this";
	document[ "extra" ] = 123;
	writeMarker( document );
	Json::Value change { Json::objectValue };
	change[ "name" ] = name + "-renamed";
	CHECK( api().patch( std::format( "/clusters/{}/modify", cluster_id ), change ).status == 200 );
	CHECK( marker() == document );
	CHECK( api().del( std::format( "/clusters/{}/remove", cluster_id ) ).status == 200 );
	cluster_id = 0;
	CHECK( std::filesystem::is_regular_file( root / ".idhan-cluster.json" ) );
	create( true );
	CHECK( marker() == document );
}

TEST_CASE_METHOD(
	ClusterMarkerFixture,
	"Readonly clusters scan and serve media without creating markers",
	"[api][cluster-markers]" )
{
	const std::string bytes { "readonly media without a marker" };
	orphan( bytes, true );
	std::filesystem::permissions(
		root,
		std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec,
		std::filesystem::perm_options::replace );
	create( true );
	CHECK_FALSE( std::filesystem::exists( root / ".idhan-cluster.json" ) );
	testServer().restart( false );
	scan( { { "adopt_orphans", "true" }, { "verify_hash", "true" } } );
	const auto hash { SHA256::hash( std::as_bytes( std::span { bytes.data(), bytes.size() } ) ) };
	RecordID record_id {};
	{
		pqxx::work transaction { db() };
		record_id = transaction.query_value< RecordID >(
			"SELECT record_id FROM file_info JOIN records USING (record_id) WHERE sha256 = decode($1, 'hex')",
			pqxx::params { hash.hex() } );
	}
	CHECK( api().get( std::format( "/records/{}/file", record_id ) ).body == bytes );
	CHECK_FALSE( std::filesystem::exists( root / ".idhan-cluster.json" ) );

	Json::Value change { Json::objectValue };
	change[ "readonly" ] = false;
	REQUIRE( api().patch( std::format( "/clusters/{}/modify", cluster_id ), change ).status == 409 );
	CHECK( api().get( std::format( "/clusters/{}/info", cluster_id ) ).json[ "readonly" ].asBool() );
	CHECK_FALSE( std::filesystem::exists( root / ".idhan-cluster.json" ) );
	std::filesystem::permissions( root, std::filesystem::perms::owner_write, std::filesystem::perm_options::add );
	REQUIRE( api().patch( std::format( "/clusters/{}/modify", cluster_id ), change ).status == 200 );
	CHECK( std::filesystem::is_regular_file( root / ".idhan-cluster.json" ) );
	CHECK( api().get( std::format( "/records/{}/file", record_id ) ).body == bytes );
}

TEST_CASE_METHOD(
	ClusterMarkerFixture,
	"Physical aliases and copied cluster markers conflict",
	"[api][cluster-markers]" )
{
	create();
	const auto original { marker() };
	const auto alias { directory / "alias" };
	std::filesystem::create_directory_symlink( root, alias );
	CHECK( api().post( "/clusters/add", body( alias, "-alias" ) ).status == 409 );
	const auto copied { directory / "copied" };
	std::filesystem::create_directory( copied );
	std::filesystem::copy_file( root / ".idhan-cluster.json", copied / ".idhan-cluster.json" );
	CHECK( api().post( "/clusters/add", body( copied, "-copy" ) ).status == 409 );
	CHECK( marker() == original );
}

TEST_CASE_METHOD(
	ClusterMarkerFixture,
	"Missing and mismatched markers block media access without detaching records",
	"[api][cluster-markers]" )
{
	create();
	const auto original { marker() };
	const std::string bytes { "IDHAN guarded cluster contents" };
	const auto imported {
		api().postOctets( "/file/import", bytes, { { "force_import", "true" }, { "filename", "marker.bin" } } )
	};
	REQUIRE( imported.status == 200 );
	REQUIRE( imported.json[ "status" ].asInt() == 1 );
	const auto record_id { imported.json[ "record_id" ].asInt() };
	const auto file_path { std::format( "/records/{}/file", record_id ) };
	CHECK( api().get( file_path ).body == bytes );
	std::filesystem::remove( root / ".idhan-cluster.json" );
	CHECK( api().get( file_path ).status == 503 );
	CHECK( api().postWithoutBody( std::format( "/clusters/{}/scan", cluster_id ) ).status == 503 );
	CHECK( api().postOctets( "/file/import", bytes, { { "force_import", "true" } } ).status == 503 );
	CHECK_FALSE( std::filesystem::exists( root / ".idhan-cluster.json" ) );
	{
		pqxx::work transaction { db() };
		CHECK(
			transaction.query_value< ClusterID >(
				"SELECT cluster_id FROM file_info WHERE record_id = $1", pqxx::params { record_id } )
			== cluster_id );
	}
	auto wrong { original };
	wrong[ "cluster_uuid" ] = boost::uuids::to_string( boost::uuids::random_generator {}() );
	writeMarker( wrong );
	CHECK( api().get( file_path ).status == 503 );
	CHECK( api().postWithoutBody( std::format( "/clusters/{}/scan", cluster_id ) ).status == 503 );
	CHECK( marker() == wrong );
	writeMarker( original );
	CHECK( api().get( file_path ).body == bytes );
}

TEST_CASE_METHOD(
	ClusterMarkerFixture,
	"Cluster scans stop admitting files after a failure and drain scheduled work",
	"[api][cluster-scan]" )
{
	create();
	const auto folder { root / "invalid" };
	std::filesystem::create_directory( folder );
	for ( int index {}; index < 12; ++index )
	{
		std::ofstream file { folder / std::format( "invalid-{}.bin", index ) };
		file << "malformed filename";
	}

	const auto status { scanStatus(
		{ { "stop_on_fail", "true" },
		  { "concurrency", "3" },
		  { "adopt_orphans", "true" },
		  { "verify_hash", "true" } } ) };
	const auto stopped { status[ "response" ] };
	const auto attempted { stopped[ "scanned_count" ].asUInt64() };
	CHECK( attempted >= 1 );
	CHECK( attempted <= 3 );
	CHECK( stopped[ "failed_count" ].asUInt64() == attempted );
	CHECK( stopped[ "failures" ].size() == attempted );
	CHECK( stopped[ "failures" ][ 0 ][ "error" ].asString().starts_with( "Invalid hash in filename:" ) );
	const auto progress { status[ "progress" ] };
	CHECK( progress[ "phase" ].asString() == "stopped" );
	CHECK( progress[ "total_files" ].asUInt64() == 12 );
	CHECK( progress[ "processed_files" ].asUInt64() == attempted );
	CHECK( progress[ "remaining_files" ].asUInt64() == 12 - attempted );
	CHECK( progress[ "failed_count" ].asUInt64() == attempted );
	CHECK( progress[ "tasks" ].empty() );

	const auto complete { scan( { { "stop_on_fail", "false" }, { "concurrency", "3" } } ) };
	CHECK( complete[ "scanned_count" ].asUInt64() == 12 );
	CHECK( complete[ "failed_count" ].asUInt64() == 12 );
}

TEST_CASE_METHOD(
	ClusterMarkerFixture,
	"Cluster scans publish bounded live subtasks and retain final progress",
	"[api][cluster-scan][scan-progress]" )
{
	create();
	for ( int index {}; index < 3; ++index )
	{
		const auto path { orphan( std::format( "progress file {}", index ), true ) };
		pqxx::work transaction { db() };
		transaction.exec(
			"INSERT INTO records (sha256) VALUES (decode($1, 'hex'))", pqxx::params { path.stem().string() } );
		transaction.commit();
	}

	// Hold record lookups so live progress is observable without timing assumptions about hashing or modules.
	pqxx::work blocked { db() };
	blocked.exec( "LOCK TABLE records IN ACCESS EXCLUSIVE MODE" );
	const auto queued { api().postWithoutBody(
		std::format( "/clusters/{}/scan", cluster_id ),
		{ { "concurrency", "2" }, { "scan_mime", "false" }, { "scan_metadata", "false" } } ) };
	REQUIRE( queued.status == 200 );
	const auto job_id { queued.json[ "job_id" ].as< JobID >() };
	const auto route { std::format( "/jobs/{}/status", job_id ) };
	const auto deadline { std::chrono::steady_clock::now() + std::chrono::seconds( 15 ) };
	Json::Value running;
	while ( std::chrono::steady_clock::now() < deadline )
	{
		const auto status { api().get( route ) };
		REQUIRE( status.status == 200 );
		running = status.json;
		if ( running[ "progress" ][ "active_files" ].asUInt64() == 2 ) break;

		std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
	}
	REQUIRE( running[ "progress" ][ "active_files" ].asUInt64() == 2 );
	CHECK_FALSE( running[ "completed" ].asBool() );
	REQUIRE( running[ "continuous" ].isBool() );
	CHECK_FALSE( running[ "continuous" ].asBool() );
	const auto progress { running[ "progress" ] };
	CHECK( progress[ "kind" ].asString() == "cluster_scan" );
	CHECK( progress[ "phase" ].asString() == "scanning_files" );
	CHECK( progress[ "total_files" ].asUInt64() == 3 );
	CHECK( progress[ "remaining_files" ].asUInt64() == 3 );
	CHECK( progress[ "processed_files" ].asUInt64() == 0 );
	REQUIRE( progress[ "tasks" ].size() == 2 );
	for ( const auto& task : progress[ "tasks" ] )
	{
		CHECK( task[ "stage" ].asString() == "checking_record" );
		CHECK( std::filesystem::is_regular_file( root / task[ "path" ].asString() ) );
	}
	const auto listed { api().get( "/jobs/status" ) };
	REQUIRE( listed.status == 200 );
	bool found { false };
	for ( const auto& job : listed.json[ "jobs" ] )
	{
		if ( job[ "job_id" ].as< JobID >() != job_id ) continue;

		CHECK( job[ "progress" ] == progress );
		found = true;
	}
	CHECK( found );
	blocked.commit();

	Json::Value finished;
	while ( std::chrono::steady_clock::now() < deadline )
	{
		finished = api().get( route ).json;
		if ( finished[ "completed" ].asBool() ) break;

		std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
	}
	REQUIRE( finished[ "completed" ].asBool() );
	CHECK_FALSE( finished[ "continuous" ].asBool() );
	CHECK( finished[ "progress" ][ "phase" ].asString() == "completed" );
	CHECK( finished[ "progress" ][ "processed_files" ].asUInt64() == 3 );
	CHECK( finished[ "progress" ][ "remaining_files" ].asUInt64() == 0 );
	CHECK( finished[ "progress" ][ "tasks" ].empty() );
	CHECK( finished[ "response" ][ "failed_count" ].asUInt64() == 0 );
}

TEST_CASE_METHOD(
	ClusterMarkerFixture,
	"Cluster scans report metadata storage while database writes are blocked",
	"[api][cluster-scan][scan-progress]" )
{
	create();
	std::ifstream source { IDHAN_SOURCE_DIR "/IDHANServer/src/static/Titorv2.png", std::ios::binary };
	REQUIRE( source.is_open() );
	std::ostringstream bytes;
	bytes << source.rdbuf();
	const auto imported { api().postOctets( "/file/import", bytes.str(), { { "force_import", "true" } } ) };
	REQUIRE( imported.status == 200 );

	// Permit reads while blocking image metadata writes, leaving the parser free to finish.
	pqxx::work blocked { db() };
	blocked.exec( "LOCK TABLE image_metadata IN SHARE MODE" );
	JobEventStream events { serverPort(), api().key() };
	REQUIRE( events.waitFor( "snapshot" )[ "jobs" ].isArray() );
	const auto queued { api().postWithoutBody(
		std::format( "/clusters/{}/scan", cluster_id ), { { "rescan_metadata", "true" }, { "scan_mime", "false" } } ) };
	REQUIRE( queued.status == 200 );
	const auto route { std::format( "/jobs/{}/status", queued.json[ "job_id" ].as< JobID >() ) };
	const auto deadline { std::chrono::steady_clock::now() + std::chrono::seconds { 15 } };
	Json::Value status;
	bool storing {};
	while ( std::chrono::steady_clock::now() < deadline )
	{
		const auto response { api().get( route ) };
		REQUIRE( response.status == 200 );
		status = response.json;
		for ( const auto& task : status[ "progress" ][ "tasks" ] )
			if ( task[ "stage" ].asString() == "storing_metadata" ) storing = true;
		if ( storing || status[ "completed" ].asBool() ) break;

		std::this_thread::sleep_for( std::chrono::milliseconds { 20 } );
	}
	REQUIRE( storing );
	CHECK_FALSE( status[ "completed" ].asBool() );
	const auto streamed { events.waitFor( "update", queued.json[ "job_id" ].as< JobID >(), "storing_metadata" ) };
	REQUIRE_FALSE( streamed.isNull() );
	CHECK_FALSE( streamed[ "completed" ].asBool() );
	blocked.commit();

	while ( std::chrono::steady_clock::now() < deadline )
	{
		status = api().get( route ).json;
		if ( status[ "completed" ].asBool() ) break;

		std::this_thread::sleep_for( std::chrono::milliseconds { 20 } );
	}
	REQUIRE( status[ "completed" ].asBool() );
	CHECK( status[ "status" ].asString() == "completed" );
	CHECK( status[ "response" ][ "scanned_count" ].asUInt64() == 1 );
	CHECK( status[ "response" ][ "failed_count" ].asUInt64() == 0 );
}

TEST_CASE_METHOD(
	ClusterMarkerFixture,
	"Empty cluster scans publish a completed zero-file total",
	"[api][cluster-scan][scan-progress]" )
{
	create();
	const auto status { scanStatus() };
	CHECK( status[ "progress" ][ "phase" ].asString() == "completed" );
	CHECK( status[ "progress" ][ "total_files" ].asUInt64() == 0 );
	CHECK( status[ "progress" ][ "remaining_files" ].asUInt64() == 0 );
	CHECK( status[ "response" ][ "scanned_count" ].asUInt64() == 0 );
}

TEST_CASE_METHOD(
	ClusterMarkerFixture,
	"Read-only scans reject misplaced orphans before creating records",
	"[api][cluster-scan]" )
{
	create( true );
	const auto path { orphan( "misplaced read-only orphan", false ) };
	const auto result { scan( { { "adopt_orphans", "true" }, { "verify_hash", "true" } } ) };
	CHECK( result[ "scanned_count" ].asUInt64() == 1 );
	CHECK( result[ "failed_count" ].asUInt64() == 1 );
	CHECK( result[ "failures" ][ 0 ][ "error" ].asString().starts_with( "File is outside its hash directory:" ) );
	CHECK( std::filesystem::exists( path ) );
	pqxx::work transaction { db() };
	CHECK(
		transaction.query_value< std::int64_t >(
			"SELECT count(*) FROM records WHERE sha256 = decode($1, 'hex')", pqxx::params { path.stem().string() } )
		== 0 );
}

TEST_CASE_METHOD(
	ClusterMarkerFixture,
	"Writable scans adopt misplaced orphans and leave unchanged file rows untouched",
	"[api][cluster-scan]" )
{
	create();
	const std::string bytes { "writable orphan contents" };
	const auto original { orphan( bytes, false ) };
	const auto adopted { scan( { { "adopt_orphans", "true" } } ) };
	REQUIRE( adopted[ "failed_count" ].asUInt64() == 0 );
	CHECK( adopted[ "scanned_count" ].asUInt64() == 1 );
	CHECK_FALSE( std::filesystem::exists( original ) );
	const auto hash { *SHA256::fromHex( original.stem().string() ) };
	CHECK( std::filesystem::exists( root / filesystem::getClusterRelativePath( hash, "bin" ) ) );

	RecordID record_id {};
	std::string version;
	{
		pqxx::work transaction { db() };
		const auto row {
			transaction
				.exec(
					"SELECT fi.record_id, fi.xmin::text FROM file_info fi JOIN records USING (record_id) "
					"WHERE sha256 = decode($1, 'hex')",
					pqxx::params { hash.hex() } )
				.one_row()
		};
		record_id = row[ 0 ].as< RecordID >();
		version = row[ 1 ].as< std::string >();
	}
	CHECK( api().get( std::format( "/records/{}/file", record_id ) ).body == bytes );
	const auto unchanged { scan( { { "scan_metadata", "false" }, { "scan_mime", "false" } } ) };
	CHECK( unchanged[ "failed_count" ].asUInt64() == 0 );
	CHECK( unchanged[ "scanned_count" ].asUInt64() == 1 );
	{
		pqxx::work transaction { db() };
		CHECK(
			transaction.query_value< std::string >(
				"SELECT xmin::text FROM file_info WHERE record_id = $1", pqxx::params { record_id } )
			== version );
	}
}

TEST_CASE_METHOD(
	ClusterMarkerFixture,
	"Metadata failures remain nonfatal for imports and are reported by cluster scans",
	"[api][cluster-scan]" )
{
	create();
	const std::string bytes { "no metadata parser for these imported bytes" };
	const auto imported { api().postOctets( "/file/import", bytes, { { "force_import", "true" } } ) };
	REQUIRE( imported.status == 200 );
	REQUIRE( imported.json[ "status" ].asInt() == 1 );
	const auto result { scan() };
	CHECK( result[ "scanned_count" ].asUInt64() == 1 );
	CHECK( result[ "failed_count" ].asUInt64() == 1 );
	CHECK( result[ "failures" ][ 0 ][ "error" ].asString().starts_with( "No parser found for mime id" ) );

	{
		pqxx::work transaction { db() };
		transaction.exec(
			"INSERT INTO metadata (record_id, simple_mime_type) VALUES ($1, $2)",
			pqxx::params {
				imported.json[ "record_id" ].as< RecordID >(), std::to_underlying( SimpleMimeType::NONE ) } );
		transaction.commit();
	}
	CHECK( scan()[ "failed_count" ].asUInt64() == 0 );
	const auto forced { scan( { { "rescan_metadata", "true" } } ) };
	CHECK( forced[ "failed_count" ].asUInt64() == 1 );
	CHECK( forced[ "failures" ][ 0 ][ "error" ].asString().starts_with( "No parser found for mime id" ) );
}

TEST_CASE_METHOD(
	ClusterMarkerFixture,
	"Explicit hash verification checks existing files even when their size and timestamps match",
	"[api][cluster-scan]" )
{
	create();
	const auto path { orphan( "original", true ) };
	REQUIRE( scan( { { "adopt_orphans", "true" } } )[ "failed_count" ].asUInt64() == 0 );
	const auto mtime { std::filesystem::last_write_time( path ) };
	{
		std::ofstream file { path, std::ios::binary };
		file << "modified";
	}
	std::filesystem::last_write_time( path, mtime );
	const auto result { scan( { { "verify_hash", "true" }, { "scan_mime", "false" }, { "scan_metadata", "false" } } ) };
	CHECK( result[ "failed_count" ].asUInt64() == 1 );
	CHECK( result[ "failures" ][ 0 ][ "error" ].asString().starts_with( "Filename does not match SHA-256:" ) );
}

TEST_CASE_METHOD(
	ClusterMarkerFixture,
	"Cluster scan enumeration excludes root files thumbnails quarantined files and symlinks",
	"[api][cluster-scan]" )
{
	create();
	const auto path { orphan( "one real file", true ) };
	std::filesystem::create_directories( root / "bad" );
	std::filesystem::create_directories( path.parent_path() / "nested" );
	for ( const auto& skipped :
	      { root / "root.bin",
	        root / "bad" / "bad.bin",
	        path.parent_path() / "ignored.thumbnail",
	        path.parent_path() / "nested" / "nested.bin" } )
	{
		std::ofstream file { skipped };
		file << "must not be scanned";
	}
	std::filesystem::create_directory_symlink( path.parent_path(), root / "folder-link" );
	std::filesystem::create_symlink( path, path.parent_path() / "file-link.bin" );
	const auto result { scan( { { "adopt_orphans", "true" } } ) };
	CHECK( result[ "scanned_count" ].asUInt64() == 1 );
	CHECK( result[ "failed_count" ].asUInt64() == 0 );
}

TEST_CASE_METHOD(
	ClusterMarkerFixture,
	"Missing-file preflight visits every page while detaching files and caps failure details",
	"[api][cluster-scan]" )
{
	create( true );
	{
		pqxx::work transaction { db() };
		transaction.exec(
			"WITH inserted AS (INSERT INTO records (sha256) "
			"SELECT digest('missing-' || number::text, 'sha256') FROM generate_series(1, 1025) AS number "
			"RETURNING record_id) INSERT INTO file_info (record_id, cluster_id, size, extension) "
			"SELECT record_id, $1, 1, 'bin' FROM inserted",
			pqxx::params { cluster_id } );
		transaction.commit();
	}
	const auto status { scanStatus( { { "remove_missing_files", "true" } } ) };
	const auto result { status[ "response" ] };
	CHECK( status[ "progress" ][ "checked_records" ].asUInt64() == 1025 );
	CHECK( status[ "progress" ][ "failed_count" ].asUInt64() == 1025 );
	CHECK( result[ "scanned_count" ].asUInt64() == 0 );
	CHECK( result[ "failed_count" ].asUInt64() == 1025 );
	CHECK( result[ "failures" ].size() == 1000 );
	pqxx::work transaction { db() };
	CHECK(
		transaction.query_value< std::int64_t >(
			"SELECT count(*) FROM file_info WHERE cluster_id = $1", pqxx::params { cluster_id } )
		== 0 );
}

} // namespace idhan::test
