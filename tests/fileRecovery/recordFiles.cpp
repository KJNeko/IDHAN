#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <catch2/catch_test_macros.hpp>
#include <folly/coro/BlockingWait.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <pqxx/pqxx>

#include <atomic>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <unistd.h>

#include "TestConnection.hpp"
#include "db/fileInfo.hpp"
#include "db/searchPath.hpp"
#include "filesystem/clusters/ClusterManager.hpp"
#include "filesystem/clusters/ClusterMarker.hpp"
#include "filesystem/recordFilePath.hpp"
#include "idhan/db/DBContext.hpp"
#include "notifications/MissingFiles.hpp"
#include "urls/AssociateUrls.hpp"

namespace idhan::test
{
std::atomic_uint missing_notifications {};
std::atomic_uint found_notifications {};
} // namespace idhan::test

// Observe the notification seams while the production storage and database helpers run.
namespace idhan::notifications
{
void onFileMissing( const RecordID record_id )
{
	CHECK( record_id == 1 );
	++test::missing_notifications;
}

void onFileFound( const RecordID record_id )
{
	CHECK( record_id == 1 );
	++test::found_notifications;
}
} // namespace idhan::notifications

namespace idhan::test
{

filesystem::ClusterManager& recoveryClusterManager()
{
	static filesystem::ClusterManager manager {};
	return manager;
}

class RecordFilesFixture
{
	static inline std::atomic_uint s_next {};
	folly::CPUThreadPoolExecutor m_executor { 1 };
	pqxx::connection m_sql { connectionString() };
	std::string m_schema { std::format( "test_file_recovery_{}_{}", ::getpid(), s_next++ ) };
	std::unique_ptr< db::DBContext > m_context {};

  public:

	const std::filesystem::path directory { std::filesystem::temp_directory_path() / m_schema };
	const std::filesystem::path cluster_directory { directory / "cluster" };
	const std::string bytes { "file recovery fixture" };
	const SHA256 hash { SHA256::hash( std::as_bytes( std::span { bytes } ) ) };
	const std::filesystem::path file { cluster_directory / filesystem::getClusterRelativePath( hash, "png" ) };

	RecordFilesFixture()
	{
		missing_notifications = 0;
		found_notifications = 0;
		std::filesystem::create_directories( cluster_directory );
		const auto uuid { boost::uuids::random_generator {}() };
		const auto root { folly::File::makeFile( cluster_directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC ) };
		REQUIRE( root.hasValue() );
		REQUIRE( filesystem::initializeClusterMarker( root->fd(), uuid ).has_value() );
		{
			pqxx::work tx { m_sql };
			tx.exec( "CREATE SCHEMA " + tx.quote_name( m_schema ) );
			tx.exec( "SET search_path = " + db::makeSearchPath( m_schema ) );
			// A scratch schema for the storage helpers, with no extensions or live application data.
			tx.exec( R"SQL(
CREATE TABLE records (record_id integer PRIMARY KEY, sha256 bytea UNIQUE NOT NULL);
CREATE TABLE mime (mime_id smallint PRIMARY KEY, best_extension text);
CREATE TABLE file_info (record_id integer PRIMARY KEY, cluster_id smallint, mime_id smallint,
    extension text, size bigint, cluster_store_time timestamp, cluster_delete_time timestamp,
    file_mtime timestamp, file_ctime timestamp);
CREATE TABLE file_clusters (cluster_id smallint PRIMARY KEY, folder_path text, cluster_uuid uuid,
    read_only boolean, cluster_name text, ratio_number smallint, size_limit bigint);
CREATE TYPE UrlType AS ENUM ('file', 'post', 'gallery', 'unknown');
CREATE TABLE url_domains (url_domain_id serial PRIMARY KEY, url_domain text UNIQUE NOT NULL);
CREATE TABLE urls (url_id serial PRIMARY KEY, url text UNIQUE NOT NULL, url_domain_id integer REFERENCES url_domains,
    url_type UrlType NOT NULL DEFAULT 'unknown');
CREATE TABLE url_mappings (url_id integer, record_id integer);
CREATE VIEW missing_files AS SELECT record_id FROM file_info
    WHERE cluster_id IS NULL AND cluster_delete_time IS NULL;
INSERT INTO mime VALUES (1, 'png');
INSERT INTO url_domains (url_domain) VALUES ('files.example');
INSERT INTO urls (url, url_domain_id, url_type) VALUES ('https://files.example/download', 1, 'file');
INSERT INTO url_mappings VALUES (1, 1);
)SQL" );
			static constexpr char comparison_index[] {
#embed "../../IDHANMigration/src/table_urls/430-comparison-index.sql" suffix(, )
				'\0'
			};
			tx.exec( comparison_index );
			tx.exec( "INSERT INTO records VALUES (1, decode($1, 'hex'))", pqxx::params { hash.hex() } );
			tx.exec(
				"INSERT INTO file_clusters VALUES (1, $1, $2::uuid, false, NULL, 1, 0)",
				pqxx::params { cluster_directory.string(), boost::uuids::to_string( uuid ) } );
			tx.exec(
				"INSERT INTO file_info (record_id, cluster_id, mime_id, extension, size, cluster_store_time) "
				"VALUES (1, 1, 1, 'png', $1, now())",
				pqxx::params { bytes.size() } );
			tx.commit();
		}
		const PostgresSettings settings {};
		db::DBConfig config {};
		config.host = settings.host;
		config.port = static_cast< std::uint16_t >( std::stoul( settings.port ) );
		config.database = settings.database;
		config.user = settings.user;
		config.password = settings.password;
		config.schema = m_schema;
		config.connections = 1;
		config.transaction_connections = 1;
		config.worker_threads = 1;
		m_context = std::make_unique< db::DBContext >( config, folly::getKeepAliveToken( &m_executor ) );
		run( m_context->start() );
		run( recoveryClusterManager().reloadClusters( *m_context ) );
		writeFile();
	}

	~RecordFilesFixture()
	{
		run( m_context->drain() );
		m_context.reset();
		pqxx::nontransaction tx { m_sql };
		tx.exec( "DROP SCHEMA " + tx.quote_name( m_schema ) + " CASCADE" );
		std::filesystem::permissions( cluster_directory, std::filesystem::perms::owner_all );
		std::filesystem::remove_all( directory );
	}

	template < typename T >
	T run( runtime::Task< T > task )
	{
		return folly::coro::blockingWait( std::move( task ) );
	}

	db::DBContext& context() { return *m_context; }

	void writeFile()
	{
		std::filesystem::create_directories( file.parent_path() );
		std::ofstream output { file, std::ios::binary };
		output << bytes;
		REQUIRE( output.good() );
	}

	void execute( const std::string& statement )
	{
		pqxx::nontransaction tx { m_sql };
		tx.exec( statement );
	}

	int domainCount( const std::string& domain )
	{
		pqxx::nontransaction tx { m_sql };
		return tx.query_value< int >(
			"SELECT count(*) FROM url_domains WHERE url_domain = $1", pqxx::params { domain } );
	}

	std::string urlType( const std::string& url )
	{
		pqxx::nontransaction tx { m_sql };
		return tx.query_value< std::string >( "SELECT url_type::text FROM urls WHERE url = $1", pqxx::params { url } );
	}

	bool missing()
	{
		pqxx::nontransaction tx { m_sql };
		return tx.query_value< bool >( "SELECT EXISTS (SELECT 1 FROM missing_files WHERE record_id = 1)" );
	}
};

TEST_CASE_METHOD( RecordFilesFixture, "Successful file detection uses the existing URL type", "[recovery][direct]" )
{
	SECTION( "An unknown URL" )
	{
		execute( "UPDATE urls SET url_type = 'unknown' WHERE url_id = 1" );
	}
	SECTION( "A URL previously classified as a post" )
	{
		execute( "UPDATE urls SET url_type = 'post' WHERE url_id = 1" );
	}
	SECTION( "A URL previously classified as a gallery" )
	{
		execute( "UPDATE urls SET url_type = 'gallery' WHERE url_id = 1" );
	}
	CHECK_FALSE( run( db::findStoredDirectRecordByUrl( context(), "https://files.example/download" ) ).has_value() );
	execute(
		"INSERT INTO urls (url_id, url, url_type) VALUES (2, 'https://site.example/post', 'post'), "
		"(3, 'https://site.example/gallery', 'gallery'), (4, 'https://site.example/source', 'unknown')" );
	run( db::markDirectUrls( context(), { "https://files.example/download" } ) );
	CHECK( urlType( "https://files.example/download" ) == "file" );
	CHECK( run( db::findStoredDirectRecordByUrl( context(), "https://files.example/download" ) ) == 1 );
	CHECK( urlType( "https://site.example/post" ) == "post" );
	CHECK( urlType( "https://site.example/gallery" ) == "gallery" );
	CHECK( urlType( "https://site.example/source" ) == "unknown" );
}

TEST_CASE_METHOD(
	RecordFilesFixture,
	"Parser URL types are persisted before any file is imported",
	"[recovery][url-types]" )
{
	const std::string post { "https://new.example/posts/42" };
	const auto created { run( helpers::setUrlType( post, "post", context() ) ) };
	REQUIRE( created.has_value() );
	CHECK( urlType( post ) == "post" );
	CHECK_FALSE( run( db::findStoredDirectRecordByUrl( context(), post ) ).has_value() );

	REQUIRE( run( helpers::setUrlType( post, "gallery", context() ) ).has_value() );
	CHECK( urlType( post ) == "gallery" );
	REQUIRE( run( helpers::setUrlType( post, "unknown", context() ) ).has_value() );
	CHECK( urlType( post ) == "unknown" );
	REQUIRE( run( helpers::setUrlType( "https://files.example/download", "post", context() ) ).has_value() );
	CHECK_FALSE( run( db::findStoredDirectRecordByUrl( context(), "https://files.example/download" ) ).has_value() );
	run( db::markDirectUrls( context(), { "https://files.example/download" } ) );
	CHECK( urlType( "https://files.example/download" ) == "file" );
	CHECK( run( db::findStoredDirectRecordByUrl( context(), "https://files.example/download" ) ) == 1 );

	CHECK_FALSE( run( helpers::setUrlType( "https://bad.example/post", "invalid", context() ) ).has_value() );
	CHECK( domainCount( "bad.example" ) == 0 );
}

TEST_CASE_METHOD( RecordFilesFixture, "Direct reuse checks the physical file and records absence", "[recovery]" )
{
	REQUIRE( run( db::findStoredDirectRecordByUrl( context(), "https://files.example/download" ) ) == 1 );
	const auto present { run( filesystem::findPresentRecordFilePath( context(), 1 ) ) };
	REQUIRE( present.has_value() );
	REQUIRE( present->has_value() );
	CHECK_FALSE( missing() );

	SECTION( "The file disappears" )
	{
		REQUIRE( std::filesystem::remove( file ) );
	}
	SECTION( "A directory replaces the file" )
	{
		REQUIRE( std::filesystem::remove( file ) );
		REQUIRE( std::filesystem::create_directory( file ) );
	}

	const auto absent { run( filesystem::findPresentRecordFilePath( context(), 1 ) ) };
	REQUIRE( absent.has_value() );
	CHECK_FALSE( absent->has_value() );
	CHECK( missing() );
	CHECK_FALSE( run( db::findStoredDirectRecordByUrl( context(), "https://files.example/download" ) ).has_value() );
	CHECK( run( db::findMissingRecordForHash( context(), hash ) ) == 1 );
	CHECK( missing_notifications == 1 );
	REQUIRE( run( filesystem::findPresentRecordFilePath( context(), 1 ) ).has_value() );
	CHECK( missing_notifications == 1 );
}

TEST_CASE_METHOD( RecordFilesFixture, "Unverifiable or inaccessible storage is not marked missing", "[recovery]" )
{
	SECTION( "The cluster marker disappears" )
	{
		REQUIRE( std::filesystem::remove( cluster_directory / filesystem::clusterMarkerFilename ) );
	}
	SECTION( "The file cannot be inspected" )
	{
		std::filesystem::permissions( cluster_directory, std::filesystem::perms::none );
	}

	const auto checked { run( filesystem::findPresentRecordFilePath( context(), 1 ) ) };
	CHECK_FALSE( checked.has_value() );
	CHECK_FALSE( missing() );
	CHECK( run( db::findStoredDirectRecordByUrl( context(), "https://files.example/download" ) ) == 1 );
	CHECK( missing_notifications == 0 );
}

TEST_CASE_METHOD( RecordFilesFixture, "A recovered hash leaves missing_files and can be reused again", "[recovery]" )
{
	REQUIRE( std::filesystem::remove( file ) );
	REQUIRE( run( filesystem::findPresentRecordFilePath( context(), 1 ) ).has_value() );
	REQUIRE( run( db::findMissingRecordForHash( context(), hash ) ) == 1 );
	writeFile();
	run( db::storeRecordFileInfo(
		context(),
		1,
		1,
		static_cast< std::int64_t >( bytes.size() ),
		1,
		"png",
		filesystem::FileTimes { .mtime = std::chrono::system_clock::now() } ) );
	CHECK_FALSE( missing() );
	CHECK_FALSE( run( db::findMissingRecordForHash( context(), hash ) ).has_value() );
	const auto present { run( filesystem::findPresentRecordFilePath( context(), 1 ) ) };
	REQUIRE( present.has_value() );
	CHECK( present->has_value() );
	CHECK( run( db::findStoredDirectRecordByUrl( context(), "https://files.example/download" ) ) == 1 );
	CHECK( found_notifications == 1 );
	CHECK( missing_notifications == 1 );
}

TEST_CASE_METHOD(
	RecordFilesFixture,
	"Deleted hashes remain deleted and stale observations preserve assignments",
	"[recovery]" )
{
	SECTION( "Deliberately deleted file" )
	{
		execute( "UPDATE file_info SET cluster_id = NULL, cluster_delete_time = now() WHERE record_id = 1" );
		CHECK_FALSE( missing() );
		CHECK_FALSE( run( db::findMissingRecordForHash( context(), hash ) ).has_value() );
	}
	SECTION( "A record has moved to a different cluster" )
	{
		execute( "UPDATE file_info SET cluster_id = 2 WHERE record_id = 1" );
		run( db::detachMissingRecordFile( context(), 1, 1 ) );
		CHECK_FALSE( missing() );
	}
	CHECK( missing_notifications == 0 );
	CHECK( found_notifications == 0 );
}

TEST_CASE_METHOD( RecordFilesFixture, "Direct URL lookup compares normalized identities", "[recovery][url-comparison]" )
{
	const std::string stored { "https://files.example/download?b=2&a=1&is=stable&ex=old&hm=old&" };
	execute( "UPDATE urls SET url = '" + stored + "' WHERE url_id = 1" );
	const std::string reordered { "https://files.example/download?hm=old&ex=old&is=stable&a=1&b=2" };
	CHECK( run( db::findStoredDirectRecordByUrl( context(), reordered ) ) == 1 );
	const std::string refreshed { "https://files.example/download?hm=new&ex=new&is=stable&a=1&b=2" };
	CHECK_FALSE( run( db::findStoredDirectRecordByUrl( context(), refreshed ) ).has_value() );
	CHECK( run( db::findStoredDirectRecordByUrl( context(), refreshed, { "ex", "hm" } ) ) == 1 );
	CHECK_FALSE( run( db::findStoredDirectRecordByUrl(
						  context(), "https://files.example/download?is=different&a=1&b=2", { "ex", "hm" } ) )
	                 .has_value() );
	CHECK( urlType( stored ) == "file" );

	SECTION( "Multiple spellings mapped to one record remain unambiguous" )
	{
		REQUIRE( run( helpers::associateUrls( 1, { refreshed }, context() ) ).has_value() );
		run( db::markDirectUrls( context(), { refreshed } ) );
		CHECK( run( db::findStoredDirectRecordByUrl( context(), refreshed, { "ex", "hm" } ) ) == 1 );
		CHECK( urlType( stored ) == "file" );
		CHECK( urlType( refreshed ) == "file" );
	}
	SECTION( "Conflicting record mappings prevent reuse" )
	{
		execute( "INSERT INTO url_mappings VALUES (1, 2)" );
		CHECK_FALSE( run( db::findStoredDirectRecordByUrl( context(), refreshed, { "ex", "hm" } ) ).has_value() );
	}
	SECTION( "A differently classified alias cannot hide a conflicting record" )
	{
		REQUIRE( run( helpers::associateUrls( 2, { refreshed }, context() ) ).has_value() );
		CHECK( urlType( refreshed ) == "unknown" );
		CHECK_FALSE( run( db::findStoredDirectRecordByUrl( context(), refreshed, { "ex", "hm" } ) ).has_value() );
	}
	SECTION( "Normalized lookup still detects and records a disappeared file" )
	{
		const auto record { run( db::findStoredDirectRecordByUrl( context(), refreshed, { "ex", "hm" } ) ) };
		REQUIRE( record.has_value() );
		REQUIRE( std::filesystem::remove( file ) );
		const auto present { run( filesystem::findPresentRecordFilePath( context(), *record ) ) };
		REQUIRE( present.has_value() );
		CHECK_FALSE( present->has_value() );
		CHECK( missing() );
		CHECK( missing_notifications == 1 );
		CHECK_FALSE( run( db::findStoredDirectRecordByUrl( context(), refreshed, { "ex", "hm" } ) ).has_value() );
	}
}

} // namespace idhan::test
