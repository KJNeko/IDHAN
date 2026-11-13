#include <arpa/inet.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <netinet/in.h>
#include <sys/socket.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <format>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>
#include <unistd.h>

#include "JobEventStream.hpp"
#include "ServerFixture.hpp"

namespace idhan::test
{
using namespace std::chrono_literals;

class DownloadSessionHttpFixture
{
	int m_socket { -1 };
	std::uint16_t m_port {};
	std::jthread m_worker {};
	mutable std::mutex m_cookie_mutex {};
	std::string m_cookie_header {};
	std::atomic_bool m_hold_files {};
	std::atomic_bool m_fail_files {};
	std::atomic_bool m_fail_gallery {};
	std::atomic_size_t m_file_requests {};

	void captureCookieHeader( const std::string_view request )
	{
		std::size_t offset {};
		while ( offset < request.size() )
		{
			const auto line_end { request.find( "\r\n", offset ) };
			const std::string_view line { request.substr( offset, line_end - offset ) };
			const auto separator { line.find( ':' ) };
			if ( separator != std::string_view::npos )
			{
				const std::string_view name { line.substr( 0, separator ) };
				if ( name.size() == 6
				     && std::ranges::equal(
						 name,
						 std::string_view { "cookie" },
						 []( const char left, const char right )
						 { return std::tolower( static_cast< unsigned char >( left ) ) == right; } ) )
				{
					std::string_view value { line.substr( separator + 1 ) };
					while ( !value.empty() && value.front() == ' ' ) value.remove_prefix( 1 );
					std::lock_guard lock { m_cookie_mutex };
					m_cookie_header = value;
					return;
				}
			}
			if ( line_end == std::string_view::npos ) return;
			offset = line_end + 2;
		}
	}

	[[nodiscard]] std::string responseBody( const std::string_view path ) const
	{
		if ( path == "/gallery.json" ) return std::format( R"({{"post":"{}"}})", url( "/post" ) );
		if ( path == "/file.gif" )
		{
			constexpr std::array< unsigned char, 43 > gif { {
				0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x01, 0x00, 0x01, 0x00, 0x80, 0xff, 0x00, 0xc0, 0xc0,
				0xc0, 0x00, 0x00, 0x00, 0x21, 0xf9, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x00,
				0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x02, 0x02, 0x44, 0x01, 0x00, 0x3b,
			} };
			return {
				reinterpret_cast< const char* >( gif.data() ),
				gif.size(),
			};
		}
		return {};
	}

	void serve()
	{
		while ( true )
		{
			const int client { ::accept( m_socket, nullptr, nullptr ) };
			if ( client < 0 ) return;

			std::array< char, 4096 > request {};
			const auto received { ::recv( client, request.data(), request.size(), 0 ) };
			std::string_view path {};
			if ( received > 0 )
			{
				const std::string_view text { request.data(), static_cast< std::size_t >( received ) };
				captureCookieHeader( text );
				const auto first_space { text.find( ' ' ) };
				const auto second_space { text.find( ' ', first_space + 1 ) };
				if ( first_space != std::string_view::npos && second_space != std::string_view::npos )
					path = text.substr( first_space + 1, second_space - first_space - 1 );
			}

			while ( path == "/file.gif" && m_hold_files.load() ) std::this_thread::sleep_for( 5ms );

			if ( path == "/file.gif" ) ++m_file_requests;

			const bool failed {
				( path == "/file.gif" && m_fail_files.load() ) || ( path == "/gallery.json" && m_fail_gallery.load() )
			};
			const std::string body { failed ? std::string {} : responseBody( path ) };
			const std::string response { std::format(
				"HTTP/1.1 {}\r\nContent-Type: {}\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}",
				failed       ? "500 Internal Server Error" :
				body.empty() ? "404 Not Found" :
							   "200 OK",
				path == "/gallery.json" ? "application/json" : "application/octet-stream",
				body.size(),
				body ) };
			(void)::send( client, response.data(), response.size(), MSG_NOSIGNAL );
			::close( client );
		}
	}

  public:

	[[nodiscard]] std::size_t fileRequests() const { return m_file_requests.load(); }

	DownloadSessionHttpFixture()
	{
		m_socket = ::socket( AF_INET, SOCK_STREAM, 0 );
		if ( m_socket < 0 )
			throw std::runtime_error(
				std::string { "Could not create downloader fixture socket: " } + std::strerror( errno ) );

		const int reuse { 1 };
		(void)::setsockopt( m_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );
		sockaddr_in address {};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
		address.sin_port = 0;
		if ( ::bind( m_socket, reinterpret_cast< sockaddr* >( &address ), sizeof( address ) ) != 0
		     || ::listen( m_socket, 8 ) != 0 )
		{
			::close( m_socket );
			throw std::runtime_error( "Could not bind downloader fixture socket" );
		}

		socklen_t length { sizeof( address ) };
		if ( ::getsockname( m_socket, reinterpret_cast< sockaddr* >( &address ), &length ) != 0 )
		{
			::close( m_socket );
			throw std::runtime_error( "Could not read downloader fixture port" );
		}
		m_port = ntohs( address.sin_port );
		m_worker = std::jthread { [ this ] { serve(); } };
	}

	~DownloadSessionHttpFixture()
	{
		m_hold_files.store( false );
		if ( m_socket >= 0 )
		{
			::shutdown( m_socket, SHUT_RDWR );
			::close( m_socket );
		}
	}

	void holdFiles( const bool hold ) { m_hold_files.store( hold ); }

	void failFiles( const bool fail ) { m_fail_files.store( fail ); }

	void failGallery( const bool fail ) { m_fail_gallery.store( fail ); }

	[[nodiscard]] std::string url( const std::string_view path ) const
	{
		return std::format( "http://127.0.0.1:{}{}", m_port, path );
	}

	[[nodiscard]] std::string cookieHeader() const
	{
		std::lock_guard lock { m_cookie_mutex };
		return m_cookie_header;
	}
};

constexpr auto wait_deadline { 60s };

class ImportCluster
{
	pqxx::connection& m_connection;
	ApiClient* m_api {};
	ClusterID m_cluster_id {};
	std::filesystem::path m_path;

  public:

	ImportCluster( pqxx::connection& connection, std::filesystem::path path ) :
	  m_connection( connection ),
	  m_path( std::move( path ) )
	{
		std::filesystem::remove_all( m_path );
	}

	ImportCluster( const ImportCluster& ) = delete;
	ImportCluster& operator=( const ImportCluster& ) = delete;

	~ImportCluster()
	{
		std::error_code filesystem_error {};
		std::filesystem::remove_all( m_path, filesystem_error );

		try
		{
			pqxx::work tx { m_connection };
			tx.exec(
				"DELETE FROM file_info WHERE cluster_id IN "
				"(SELECT cluster_id FROM file_clusters WHERE folder_path = $1)",
				pqxx::params { m_path.string() } );
			tx.commit();
			if ( m_api ) CHECK( m_api->del( std::format( "/clusters/{}/remove", m_cluster_id ) ).status == 200 );
		}
		catch ( const std::exception& e )
		{
			WARN( "Failed to clean up the download session cluster: " << e.what() );
		}
	}

	void registerWith( ApiClient& api )
	{
		m_api = &api;
		std::filesystem::create_directories( m_path );

		Json::Value body {};
		body[ "name" ] = "download session imports";
		body[ "path" ] = m_path.string();
		body[ "readonly" ] = false;

		const auto added { api.post( "/clusters/add", body ) };
		INFO( added.body );
		REQUIRE( added.status == 201 );
		m_cluster_id = static_cast< ClusterID >( added.json[ "cluster_id" ].asInt() );
	}
};

static bool isFinishedUrlState( const std::string_view state )
{
	return state != "pending" && state != "processing";
}

Json::Value waitForDownloadJob( ApiClient& api, const DownloadSessionID session_id, const DownloadSessionUrlID job_id )
{
	const auto deadline { std::chrono::steady_clock::now() + wait_deadline };

	while ( std::chrono::steady_clock::now() < deadline )
	{
		const auto listed { api.get( std::format( "/download_sessions/{}/urls", session_id ) ) };
		if ( listed.status != 200 ) throw std::runtime_error( "Could not list download jobs: " + listed.body );

		for ( const auto& job : listed.json )
		{
			if ( job[ "id" ].as< DownloadSessionUrlID >() != job_id ) continue;
			if ( isFinishedUrlState( job[ "state" ].asString() ) ) return job;
		}
		std::this_thread::sleep_for( 25ms );
	}
	throw std::runtime_error( "Timed out waiting for download job" );
}

Json::Value waitForFlattenedRoot(
	ApiClient& api,
	const DownloadSessionID session_id,
	const DownloadSessionUrlID job_id )
{
	const auto deadline { std::chrono::steady_clock::now() + wait_deadline };

	while ( std::chrono::steady_clock::now() < deadline )
	{
		const auto listed { api.get( std::format( "/download_sessions/{}/urls?flatten=true", session_id ) ) };
		if ( listed.status != 200 )
			throw std::runtime_error( "Could not list flattened download jobs: " + listed.body );

		for ( const auto& job : listed.json )
		{
			if ( job[ "id" ].as< DownloadSessionUrlID >() != job_id ) continue;
			std::size_t imported_urls {};
			for ( const auto& url : job[ "urls" ] )
				if ( url[ "record_id" ].isIntegral() ) ++imported_urls;
			if ( imported_urls == 2 ) return job;
		}
		std::this_thread::sleep_for( 25ms );
	}
	throw std::runtime_error( "Timed out waiting for imported child URLs" );
}

Json::Value waitForImportedDownload(
	ApiClient& api,
	const DownloadSessionID session_id,
	const DownloadSessionUrlID job_id )
{
	const auto deadline { std::chrono::steady_clock::now() + wait_deadline };
	while ( std::chrono::steady_clock::now() < deadline )
	{
		const auto listed { api.get( std::format( "/download_sessions/{}/urls?flatten=true", session_id ) ) };
		if ( listed.status != 200 ) throw std::runtime_error( "Could not list download jobs: " + listed.body );

		for ( const auto& root : listed.json )
		{
			if ( root[ "id" ].as< DownloadSessionUrlID >() != job_id ) continue;

			for ( const auto& child : root[ "urls" ] )
			{
				if ( child[ "state" ].asString() == "failed" )
					throw std::runtime_error( "Import failed: " + child[ "error" ].asString() );

				if ( child[ "record_id" ].isIntegral() ) return child;
			}
		}
		std::this_thread::sleep_for( 25ms );
	}
	throw std::runtime_error( "Timed out waiting for a downloaded record" );
}

static Json::Value waitForErrorCount( ApiClient& api, const DownloadSessionID session_id, const Json::ArrayIndex count )
{
	const auto deadline { std::chrono::steady_clock::now() + wait_deadline };
	const auto path { std::format( "/download_sessions/{}/errors", session_id ) };

	while ( std::chrono::steady_clock::now() < deadline )
	{
		const auto listed { api.get( path ) };
		if ( listed.status != 200 ) throw std::runtime_error( "Could not list download errors: " + listed.body );

		if ( listed.json[ "errors" ].size() == count ) return listed.json;

		std::this_thread::sleep_for( 25ms );
	}
	throw std::runtime_error( std::format( "Timed out waiting for {} download error(s)", count ) );
}

TEST_CASE_METHOD(
	ServerFixture,
	"A failed retry keeps a URL's errors and a passing retry removes them",
	"[api][download-sessions][retry]" )
{
	DownloadSessionHttpFixture fixture {};
	ImportCluster cluster { db(), std::filesystem::temp_directory_path() / "idhan-download-retry-cluster" };
	cluster.registerWith( api() );

	Json::Value create_body {};
	create_body[ "name" ] = "retry errors";
	const auto created { api().post( "/download_sessions", create_body ) };
	REQUIRE( created.status == 201 );
	const auto session_id { created.json[ "id" ].as< DownloadSessionID >() };

	fixture.failGallery( true );

	Json::Value url_body {};
	url_body[ "url" ] = fixture.url( "/gallery" );
	const auto submitted { api().post( std::format( "/download_sessions/{}/urls", session_id ), url_body ) };
	REQUIRE( submitted.status == 201 );
	const auto root { submitted.json[ "id" ].as< DownloadSessionUrlID >() };
	const auto retry_path { std::format( "/download_sessions/{}/urls/{}/retry", session_id, root ) };

	CHECK( waitForDownloadJob( api(), session_id, root )[ "state" ].asString() == "failed" );
	(void)waitForErrorCount( api(), session_id, 1 );

	REQUIRE( api().post( retry_path, Json::Value {} ).status == 200 );
	CHECK( waitForDownloadJob( api(), session_id, root )[ "state" ].asString() == "failed" );
	(void)waitForErrorCount( api(), session_id, 2 );

	fixture.failGallery( false );

	REQUIRE( api().post( retry_path, Json::Value {} ).status == 200 );
	CHECK( waitForDownloadJob( api(), session_id, root )[ "state" ].asString() == "completed" );
	(void)waitForErrorCount( api(), session_id, 0 );

	const auto session { api().get( std::format( "/download_sessions/{}", session_id ) ) };
	REQUIRE( session.status == 200 );
	CHECK( session.json[ "error_count" ].asInt64() == 0 );
}

TEST_CASE_METHOD(
	ServerFixture,
	"A retry replaces a child import's error and removes it once the import passes",
	"[api][download-sessions][retry]" )
{
	DownloadSessionHttpFixture fixture {};
	ImportCluster cluster { db(), std::filesystem::temp_directory_path() / "idhan-download-retry-child-cluster" };
	cluster.registerWith( api() );

	Json::Value create_body {};
	create_body[ "name" ] = "retry child errors";
	const auto created { api().post( "/download_sessions", create_body ) };
	REQUIRE( created.status == 201 );
	const auto session_id { created.json[ "id" ].as< DownloadSessionID >() };
	const auto errors_path { std::format( "/download_sessions/{}/errors", session_id ) };

	fixture.failFiles( true );

	Json::Value url_body {};
	url_body[ "url" ] = fixture.url( "/post" );
	const auto submitted { api().post( std::format( "/download_sessions/{}/urls", session_id ), url_body ) };
	REQUIRE( submitted.status == 201 );
	const auto root { submitted.json[ "id" ].as< DownloadSessionUrlID >() };
	const auto retry_path { std::format( "/download_sessions/{}/urls/{}/retry", session_id, root ) };

	// A URL that completes on its first attempt keeps any errors that attempt logged.
	CHECK( waitForDownloadJob( api(), session_id, root )[ "state" ].asString() == "completed" );
	const auto first_errors { waitForErrorCount( api(), session_id, 1 ) };
	CHECK( first_errors[ "errors" ][ 0 ][ "url" ].asString() == fixture.url( "/file.gif" ) );
	const auto first_error { first_errors[ "errors" ][ 0 ][ "id" ].asInt64() };

	REQUIRE( api().post( retry_path, Json::Value {} ).status == 200 );

	bool replaced {};
	const auto deadline { std::chrono::steady_clock::now() + wait_deadline };
	while ( !replaced && std::chrono::steady_clock::now() < deadline )
	{
		const auto listed { api().get( errors_path ) };
		REQUIRE( listed.status == 200 );

		const auto& errors { listed.json[ "errors" ] };
		replaced = errors.size() == 1 && errors[ 0 ][ "id" ].asInt64() != first_error;

		if ( !replaced ) std::this_thread::sleep_for( 25ms );
	}
	CHECK( replaced );

	fixture.failFiles( false );

	REQUIRE( api().post( retry_path, Json::Value {} ).status == 200 );
	(void)waitForImportedDownload( api(), session_id, root );
	(void)waitForErrorCount( api(), session_id, 0 );
}

TEST_CASE_METHOD(
	ServerFixture,
	"Parser tag domains are created for downloaded and reused files",
	"[api][download-sessions][tag-domain]" )
{
	DownloadSessionHttpFixture fixture {};
	ImportCluster cluster { db(), std::filesystem::temp_directory_path() / "idhan-download-domain-cluster" };
	cluster.registerWith( api() );

	Json::Value create_body {};
	create_body[ "name" ] = "parser tag domains";
	const auto created { api().post( "/download_sessions", create_body ) };
	REQUIRE( created.status == 201 );
	const auto session_id { created.json[ "id" ].as< DownloadSessionID >() };

	Json::Value url_body {};
	url_body[ "url" ] = fixture.url( "/post?postedAt=1600000000" );
	const auto submitted { api().post( std::format( "/download_sessions/{}/urls", session_id ), url_body ) };
	REQUIRE( submitted.status == 201 );
	const auto imported {
		waitForImportedDownload( api(), session_id, submitted.json[ "id" ].as< DownloadSessionUrlID >() )
	};
	const auto record_id { imported[ "record_id" ].as< RecordID >() };
	CHECK( fixture.fileRequests() == 1 );

	url_body[ "url" ] = fixture.url( "/post?domain=fixture-override&postedAt=1700000000" );
	const auto repeated { api().post( std::format( "/download_sessions/{}/urls", session_id ), url_body ) };
	REQUIRE( repeated.status == 201 );
	const auto reused {
		waitForImportedDownload( api(), session_id, repeated.json[ "id" ].as< DownloadSessionUrlID >() )
	};
	CHECK( reused[ "record_id" ].as< RecordID >() == record_id );
	CHECK( fixture.fileRequests() == 1 );
	{
		pqxx::nontransaction transaction { db() };
		const auto dates { transaction.exec_params(
			"SELECT extract(epoch FROM posted_at)::bigint, post_dates_complete FROM download_session_records "
			"WHERE download_session_id=$1 AND record_id=$2",
			session_id,
			record_id ) };
		REQUIRE( dates.size() == 1 );
		CHECK( dates[ 0 ][ 0 ].as< std::int64_t >() == 1700000000 );
		CHECK( dates[ 0 ][ 1 ].as< bool >() );
		const auto source_dates { transaction.exec(
			"SELECT extract(epoch FROM posted_at)::bigint, post_dates_complete FROM download_session_urls "
			"WHERE download_session_id=$1 AND record_id=$2 ORDER BY download_session_url_id DESC LIMIT 1",
			pqxx::params { session_id, record_id } ) };
		REQUIRE( source_dates.size() == 1 );
		CHECK( source_dates[ 0 ][ 0 ].as< std::int64_t >() == 1700000000 );
		CHECK( source_dates[ 0 ][ 1 ].as< bool >() );
	}

	url_body[ "url" ] = fixture.url( "/post?multipleDomains=1" );
	const auto multi { api().post( std::format( "/download_sessions/{}/urls", session_id ), url_body ) };
	REQUIRE( multi.status == 201 );
	const auto grouped {
		waitForImportedDownload( api(), session_id, multi.json[ "id" ].as< DownloadSessionUrlID >() )
	};
	CHECK( grouped[ "record_id" ].as< RecordID >() == record_id );
	CHECK( fixture.fileRequests() == 1 );

	{
		pqxx::nontransaction transaction { db() };
		const auto dates { transaction.exec_params(
			"SELECT extract(epoch FROM posted_at)::bigint, post_dates_complete FROM download_session_records "
			"WHERE download_session_id=$1 AND record_id=$2",
			session_id,
			record_id ) };
		REQUIRE( dates.size() == 1 );
		CHECK( dates[ 0 ][ 0 ].as< std::int64_t >() == 1700000000 );
		CHECK_FALSE( dates[ 0 ][ 1 ].as< bool >() );
	}

	const std::vector< std::pair< std::string, std::string > > expected_tags {
		{ "downloader tags", "fixture" },
		{ "fixture-override", "fixture" },
		{ "fixture-first", "first" },
		{ "fixture-second", "second" },
	};
	for ( const auto& [ name, subtag ] : expected_tags )
	{
		pqxx::nontransaction tx { db() };
		const auto domain_id { tx.query_value< TagDomainID >(
			"SELECT tag_domain_id FROM tag_domains WHERE domain_name = $1", pqxx::params { name } ) };
		CHECK(
			tx.query_value< int >(
				"SELECT COUNT(*) FROM tag_mappings WHERE record_id = $1 AND tag_domain_id = $2",
				pqxx::params { record_id, domain_id } )
		    == 1 );
		CHECK(
			tx.query_value< std::string >(
				"SELECT subtag_text FROM tag_mappings JOIN tags USING (tag_id) WHERE record_id = $1 AND tag_domain_id = $2",
				pqxx::params { record_id, domain_id } )
		    == subtag );
	}
}

SCENARIO_METHOD(
	ServerFixture,
	"Download sessions execute and persist independent URL jobs",
	"[api][download-sessions]" )
{
	DownloadSessionHttpFixture fixture {};
	ImportCluster cluster { db(), std::filesystem::temp_directory_path() / "idhan-download-session-cluster" };
	cluster.registerWith( api() );

	Json::Value create_body {};
	create_body[ "name" ] = "fixture downloads";
	const auto created { api().post( "/download_sessions", create_body ) };
	REQUIRE( created.status == 201 );
	const auto session_id { created.json[ "id" ].as< DownloadSessionID >() };

	Json::Value url_body {};
	url_body[ "url" ] = fixture.url( "/gallery" );
	const auto first { api().post( std::format( "/download_sessions/{}/urls", session_id ), url_body ) };
	REQUIRE( first.status == 201 );
	CHECK( first.json[ "state" ].asString() == "pending" );

	const auto first_job { waitForDownloadJob( api(), session_id, first.json[ "id" ].as< DownloadSessionUrlID >() ) };
	CHECK( first_job[ "state" ].asString() == "completed" );
	CHECK_FALSE( first_job[ "finished_at" ].isNull() );
	CHECK( first_job[ "error" ].isNull() );
	CHECK( fixture.cookieHeader().contains( "fringeBenefits=yup" ) );
	const auto flattened { waitForFlattenedRoot( api(), session_id, first.json[ "id" ].as< DownloadSessionUrlID >() ) };
	REQUIRE( flattened[ "urls" ].isArray() );
	REQUIRE( flattened[ "record_ids" ].isArray() );
	CHECK( flattened[ "record_ids" ].size() == 1 );

	std::size_t imported_urls {};
	std::size_t session_duplicates {};
	for ( const auto& url : flattened[ "urls" ] )
	{
		if ( url[ "record_id" ].isIntegral() ) ++imported_urls;
		if ( url[ "state" ].asString() == "imported_previously" ) ++session_duplicates;
	}
	CHECK( imported_urls == 2 );
	CHECK( session_duplicates == 1 );
	{
		pqxx::nontransaction tx { db() };
		CHECK( tx.query_value< int >( "SELECT COUNT(*) FROM tag_domains WHERE domain_name = 'downloader tags'" ) == 1 );
		CHECK( tx.query_value< bool >(
			"SELECT (SELECT COUNT(*) FROM pg_inherits WHERE inhparent = 'tag_mappings'::regclass) "
			"= (SELECT COUNT(*) FROM tag_domains)" ) );
	}

	const auto records { api().get( std::format( "/download_sessions/{}/records", session_id ) ) };
	REQUIRE( records.status == 200 );
	REQUIRE( records.json[ "record_ids" ].isArray() );
	CHECK( records.json[ "record_ids" ].size() == 1 );

	url_body[ "url" ] = fixture.url( "/failure" );
	const auto failing { api().post( std::format( "/download_sessions/{}/urls", session_id ), url_body ) };
	REQUIRE( failing.status == 201 );
	const auto failed_job {
		waitForDownloadJob( api(), session_id, failing.json[ "id" ].as< DownloadSessionUrlID >() )
	};
	CHECK( failed_job[ "state" ].asString() == "failed" );
	CHECK( failed_job[ "error" ].asString().contains( "deterministic parser failure" ) );

	url_body[ "url" ] = fixture.url( "/unmatched" );
	const auto unmatched { api().post( std::format( "/download_sessions/{}/urls", session_id ), url_body ) };
	CHECK( unmatched.status == 400 );
	const auto rejection {
		std::format( "Unable to schedule URL: No URL class accepts URL: {}", fixture.url( "/unmatched" ) )
	};
	CHECK( unmatched.body == rejection );
	const auto unmatched_session { api().post( "/download_sessions/urls", url_body ) };
	CHECK( unmatched_session.status == 400 );
	CHECK( unmatched_session.body == rejection );
	const auto jobs { api().get( std::format( "/download_sessions/{}/urls", session_id ) ) };
	REQUIRE( jobs.status == 200 );
	CHECK( jobs.json.size() == 2 );
}

SCENARIO_METHOD(
	ServerFixture,
	"Pending imports survive a server restart after the parser completes",
	"[api][download-sessions][recovery]" )
{
	const bool crash { GENERATE( false, true ) };
	DownloadSessionHttpFixture fixture {};
	fixture.holdFiles( true );
	ImportCluster cluster { db(), std::filesystem::temp_directory_path() / "idhan-download-recovery-cluster" };
	cluster.registerWith( api() );

	Json::Value body {};
	body[ "name" ] = "restart recovery";
	const auto created { api().post( "/download_sessions", body ) };
	REQUIRE( created.status == 201 );
	const auto session_id { created.json[ "id" ].as< DownloadSessionID >() };
	body = Json::Value {};
	body[ "url" ] = fixture.url( "/gallery" );
	const auto queued { api().post( std::format( "/download_sessions/{}/urls", session_id ), body ) };
	REQUIRE( queued.status == 201 );
	const auto root { queued.json[ "id" ].as< DownloadSessionUrlID >() };

	bool persisted {};
	const auto deadline { std::chrono::steady_clock::now() + wait_deadline };
	while ( std::chrono::steady_clock::now() < deadline )
	{
		pqxx::read_transaction tx { db() };
		persisted = tx.query_value< bool >(
			"SELECT count(*) = 2 FROM download_session_urls child "
			"JOIN download_session_urls parent ON parent.download_session_id = child.download_session_id "
			"AND parent.download_session_url_id = child.parent_url_id "
			"WHERE child.download_session_id = $1 AND child.state = 'pending' "
			"AND parent.state = 'completed' AND child.url = $2",
			pqxx::params { session_id, fixture.url( "/file.gif" ) } );
		if ( persisted ) break;
		std::this_thread::sleep_for( 25ms );
	}
	REQUIRE( persisted );

	testServer().restart( crash );
	const auto errors { api().get( std::format( "/download_sessions/{}/errors", session_id ) ) };
	REQUIRE( errors.status == 200 );
	CHECK( errors.json[ "errors" ].empty() );
	fixture.holdFiles( false );
	const auto restored { waitForFlattenedRoot( api(), session_id, root ) };
	CHECK( restored[ "record_ids" ].size() == 1 );
	pqxx::read_transaction tx { db() };
	CHECK(
		tx.query_value< int >(
			"SELECT count(*) FROM download_session_urls WHERE download_session_id = $1 "
			"AND state IN ('pending', 'processing')",
			pqxx::params { session_id } )
		== 0 );
}

//! Waits for a session_state event announcing the given state.
static Json::Value waitForSessionState( JobEventStream& events, const std::string_view state )
{
	for ( int attempt {}; attempt < 8; ++attempt )
	{
		auto event { events.waitFor( "session_state" ) };
		REQUIRE_FALSE( event.isNull() );
		if ( event[ "state" ].asString() == state ) return event;
	}
	FAIL( "The session never reported " << state );
	return {};
}

static std::string downloadSessionImportStates( pqxx::connection& db, const DownloadSessionID session_id )
{
	pqxx::read_transaction tx { db };
	return tx.query_value< std::string >(
		"SELECT jsonb_agg(jsonb_build_array(download_session_url_id, record_id, state, finished_at) "
		"ORDER BY download_session_url_id)::text FROM download_session_urls "
		"WHERE download_session_id = $1 AND record_id IS NOT NULL",
		pqxx::params { session_id } );
}

static Json::Value waitForBandwidth( JobEventStream& events, const bool downloading )
{
	for ( int attempt {}; attempt < 8; ++attempt )
	{
		auto event { events.waitFor( "bandwidth" ) };
		REQUIRE_FALSE( event.isNull() );
		if ( ( event[ "download_bytes_per_second" ].asUInt64() > 0 ) == downloading ) return event;
	}
	FAIL( "The stream did not report the expected download bandwidth" );
	return {};
}

TEST_CASE_METHOD(
	ServerFixture,
	"Download bandwidth reports session and global deltas and stays quiet after zero",
	"[api][download-sessions][bandwidth]" )
{
	DownloadSessionHttpFixture fixture;
	ImportCluster cluster { db(), std::filesystem::temp_directory_path() / "idhan-bandwidth-events-cluster" };
	cluster.registerWith( api() );
	std::vector< DownloadSessionID > ids;
	for ( int index {}; index < 2; ++index )
	{
		Json::Value body;
		body[ "name" ] = std::format( "bandwidth {}", index );
		const auto created { api().post( "/download_sessions", body ) };
		REQUIRE( created.status == 201 );
		ids.push_back( created.json[ "id" ].as< DownloadSessionID >() );
	}
	JobEventStream events {
		serverPort(), api().key(), std::format( "/events?filter=downloader&session_ids={}", ids.front() )
	};
	REQUIRE_FALSE( events.waitFor( "snapshot_complete" ).isNull() );
	const auto initial { events.waitFor( "bandwidth" ) };
	REQUIRE_FALSE( initial.isNull() );
	for ( const auto id : ids )
	{
		Json::Value body;
		body[ "url" ] = fixture.url( "/gallery" );
		REQUIRE( api().post( std::format( "/download_sessions/{}/urls", id ), body ).status == 201 );
	}
	const auto bandwidth { waitForBandwidth( events, true ) };
	CHECK( bandwidth.getMemberNames().size() == 3 );
	std::uint64_t total {};
	for ( const auto& session : bandwidth[ "sessions" ] )
	{
		CHECK( session.getMemberNames().size() == 2 );
		total += session[ "download_bytes_per_second" ].asUInt64();
	}
	CHECK( bandwidth[ "download_bytes_per_second" ].asUInt64() == total );
	CHECK( bandwidth[ "sessions" ].size() >= 2 );
	const auto network { events.waitFor( "network" ) };
	REQUIRE_FALSE( network.isNull() );
	CHECK_FALSE( network.isMember( "download_bytes_per_second" ) );
	(void)waitForBandwidth( events, false );
	CHECK( events.waitFor( "bandwidth", {}, {}, 2500ms ).isNull() );
	Json::Value body;
	body[ "url" ] = fixture.url( "/gallery" );
	REQUIRE( api().post( std::format( "/download_sessions/{}/urls", ids.front() ), body ).status == 201 );
	(void)waitForBandwidth( events, true );
	(void)waitForBandwidth( events, false );
}

TEST_CASE_METHOD(
	ServerFixture,
	"Restart recovery preserves new and existing imports in the session",
	"[api][download-sessions][recovery][import-history]" )
{
	DownloadSessionHttpFixture fixture;
	ImportCluster cluster { db(), std::filesystem::temp_directory_path() / "idhan-download-history-cluster" };
	cluster.registerWith( api() );
	Json::Value body;
	body[ "name" ] = "preserved import history";
	const auto created { api().post( "/download_sessions", body ) };
	REQUIRE( created.status == 201 );
	const auto session_id { created.json[ "id" ].as< DownloadSessionID >() };
	body = Json::Value {};
	body[ "url" ] = fixture.url( "/gallery" );
	const auto queued { api().post( std::format( "/download_sessions/{}/urls", session_id ), body ) };
	REQUIRE( queued.status == 201 );
	const auto root { queued.json[ "id" ].as< DownloadSessionUrlID >() };
	(void)waitForFlattenedRoot( api(), session_id, root );
	const auto original { downloadSessionImportStates( db(), session_id ) };
	REQUIRE( original.contains( "\"imported\"" ) );
	REQUIRE( original.contains( "\"imported_previously\"" ) );

	for ( int restart {}; restart < 2; ++restart )
	{
		testServer().stop();
		{
			// Model a root interrupted after its child imports were already committed.
			// This modifies only the isolated API test schema.
			pqxx::work tx { db() };
			tx.exec(
				"UPDATE download_session_urls SET state = 'processing', finished_at = NULL "
				"WHERE download_session_id = $1 AND download_session_url_id = $2",
				pqxx::params { session_id, root } );
			tx.commit();
		}
		testServer().restart( false );
		JobEventStream events {
			serverPort(), api().key(), std::format( "/events?filter=downloader&session_ids={}", session_id )
		};
		const auto snapshot { events.waitFor( "session_snapshot" ) };
		REQUIRE_FALSE( snapshot.isNull() );
		if ( snapshot[ "state" ].asString() != "idle" ) (void)waitForSessionState( events, "idle" );

		CHECK( downloadSessionImportStates( db(), session_id ) == original );
		pqxx::read_transaction tx { db() };
		CHECK(
			tx.query_value< int >(
				"SELECT count(*) FROM download_session_urls WHERE download_session_id = $1 AND record_id IS NOT NULL",
				pqxx::params { session_id } )
		    == 2 );
	}
}

//! Waits for the URL to be updated to the given state, skipping earlier updates.
static Json::Value waitForUrlState(
	JobEventStream& events,
	const DownloadSessionUrlID url_id,
	const std::string_view state )
{
	for ( int attempt {}; attempt < 16; ++attempt )
	{
		auto event { events.waitFor( "url_updated" ) };
		REQUIRE_FALSE( event.isNull() );
		if ( event[ "url_id" ].as< DownloadSessionUrlID >() != url_id ) continue;

		if ( event[ "state" ].asString() == state ) return event;
	}
	FAIL( "URL " << url_id << " never reached " << state );
	return {};
}

TEST_CASE_METHOD(
	ServerFixture,
	"Downloader SSE reports snapshots, URL changes and reactivation",
	"[api][session-events]" )
{
	DownloadSessionHttpFixture fixture;
	Json::Value body;
	body[ "name" ] = "session event lifecycle";
	const auto created { api().post( "/download_sessions", body ) };
	REQUIRE( created.status == 201 );
	const auto id { created.json[ "id" ].as< DownloadSessionID >() };
	JobEventStream events { serverPort(), api().key(), std::format( "/events?filter=downloader&session_ids={}", id ) };
	const auto snapshot { events.waitFor( "session_snapshot" ) };
	CHECK( snapshot[ "session_id" ].as< DownloadSessionID >() == id );
	CHECK( snapshot[ "state" ].asString() == "idle" );
	CHECK_FALSE( events.waitFor( "snapshot_complete" ).isNull() );

	body = Json::Value {};
	body[ "url" ] = fixture.url( "/failure" );
	const auto submitted { api().post( std::format( "/download_sessions/{}/urls", id ), body ) };
	REQUIRE( submitted.status == 201 );
	const auto url_id { submitted.json[ "id" ].as< DownloadSessionUrlID >() };

	const auto created_url { events.waitFor( "url_created" ) };
	CHECK( created_url[ "url_id" ].as< DownloadSessionUrlID >() == url_id );
	CHECK_FALSE( created_url.isMember( "state" ) );
	CHECK( waitForSessionState( events, "active" ).isMember( "last_used_at" ) );
	const auto failed { waitForUrlState( events, url_id, "failed" ) };
	CHECK( failed[ "stage" ].asString() == "parser" );
	CHECK( failed[ "error" ].asString().contains( "deterministic parser failure" ) );
	CHECK( failed.isMember( "finished_at" ) );
	(void)waitForSessionState( events, "idle" );

	REQUIRE(
		api().post( std::format( "/download_sessions/{}/urls/{}/retry", id, url_id ), Json::Value {} ).status == 200 );
	CHECK_FALSE( waitForUrlState( events, url_id, "pending" ).isMember( "error" ) );
	(void)waitForSessionState( events, "active" );
	(void)waitForUrlState( events, url_id, "failed" );
	(void)waitForSessionState( events, "idle" );
	REQUIRE( api().del( std::format( "/download_sessions/{}", id ) ).status == 200 );
}

TEST_CASE_METHOD( ServerFixture, "Downloader SSE without IDs includes future sessions", "[api][session-events]" )
{
	Json::Value body;
	body[ "name" ] = "snapshot subscription baseline";
	const auto baseline { api().post( "/download_sessions", body ) };
	REQUIRE( baseline.status == 201 );
	JobEventStream events { serverPort(), api().key(), "/events?filter=downloader" };
	(void)events.waitFor( "snapshot_complete" );
	body[ "name" ] = "future session events";
	const auto created { api().post( "/download_sessions", body ) };
	REQUIRE( created.status == 201 );
	const auto id { created.json[ "id" ].as< DownloadSessionID >() };
	CHECK( events.waitFor( "session_created" )[ "session_id" ].as< DownloadSessionID >() == id );
	REQUIRE( api().del( std::format( "/download_sessions/{}", id ) ).status == 200 );
	CHECK( events.waitFor( "session_deleted" )[ "session_id" ].as< DownloadSessionID >() == id );
	REQUIRE(
		api().del( std::format( "/download_sessions/{}", baseline.json[ "id" ].as< DownloadSessionID >() ) ).status
	    == 200 );
}

TEST_CASE_METHOD( ServerFixture, "Event subscriptions reject malformed filters", "[api][session-events]" )
{
	for ( const auto* ids : { "", "0", "-1", "abc", "1,,2", "1,", ",1", "1.5" } )
	{
		CAPTURE( ids );
		CHECK( api().get( "/events", { { "filter", "downloader" }, { "session_ids", ids } } ).status == 400 );
	}

	CHECK( api().get( "/events", { { "filter", "session" } } ).status == 400 );
}

TEST_CASE_METHOD(
	ServerFixture,
	"Direct URL failure stays on its original row",
	"[api][download-sessions][direct-row]" )
{
	DownloadSessionHttpFixture fixture;
	Json::Value body;
	body[ "url" ] = fixture.url( "/unclaimed-missing" );
	const auto submitted { api().post( "/download_sessions/urls", body ) };
	REQUIRE( submitted.status == 201 );
	const auto session_id { submitted.json[ "session" ][ "id" ].as< DownloadSessionID >() };
	const auto root_id { submitted.json[ "url" ][ "id" ].as< DownloadSessionUrlID >() };
	const auto root { waitForDownloadJob( api(), session_id, root_id ) };
	CHECK( root[ "state" ].asString() == "failed" );
	CHECK( root[ "children" ].empty() );
	REQUIRE( api().del( std::format( "/download_sessions/{}", session_id ) ).status == 200 );
}

TEST_CASE_METHOD(
	ServerFixture,
	"Hydrus Companion submits URLs and recognises imported page, file and source URLs",
	"[api][hyapi][download-sessions]" )
{
	DownloadSessionHttpFixture fixture;
	ImportCluster cluster { db(), std::filesystem::temp_directory_path() / "idhan-hyapi-import-cluster" };
	cluster.registerWith( api() );
	const QueryParams auth { { "Hydrus-Client-API-Access-Key", api().key() } };
	const auto post_url { fixture.url( "/post?source=one&view=two" ) };
	const auto unknown { api().get(
		"/hyapi/add_urls/get_url_files", { { "url", post_url }, { "Hydrus-Client-API-Access-Key", api().key() } } ) };
	REQUIRE( unknown.status == 200 );
	CHECK( unknown.json[ "url_file_statuses" ].empty() );
	CHECK( unknown.json[ "normalised_url" ].asString() == post_url );

	Json::Value body { Json::objectValue };
	body[ "url" ] = post_url;
	const auto submitted { api().post( "/hyapi/add_urls/add_url", body, auth ) };
	INFO( submitted.body );
	REQUIRE( submitted.status == 200 );
	CHECK( submitted.json[ "normalised_url" ].asString() == post_url );
	CHECK( !submitted.json[ "human_result_text" ].asString().empty() );

	const auto sessions { api().get( "/download_sessions" ) };
	REQUIRE( sessions.status == 200 );
	REQUIRE( sessions.json.size() == 1 );
	const auto session_id { sessions.json[ 0 ][ "id" ].as< DownloadSessionID >() };
	const auto urls { api().get( std::format( "/download_sessions/{}/urls", session_id ) ) };
	REQUIRE( urls.status == 200 );
	const auto root_id { urls.json[ 0 ][ "id" ].as< DownloadSessionUrlID >() };
	const auto root { waitForDownloadJob( api(), session_id, root_id ) };
	REQUIRE( root[ "state" ].asString() == "completed" );

	std::string hash;
	for ( const auto& url :
	      { post_url,
	        post_url + "#image",
	        fixture.url( "/post?view=two&source=one" ),
	        fixture.url( "/file.gif" ),
	        fixture.url( "/source" ) } )
	{
		const auto found { api().get(
			"/hyapi/add_urls/get_url_files", { { "url", url }, { "Hydrus-Client-API-Access-Key", api().key() } } ) };
		INFO( found.body );
		REQUIRE( found.status == 200 );
		const auto& statuses { found.json[ "url_file_statuses" ] };
		REQUIRE( statuses.size() == 1 );
		CHECK( statuses[ 0 ][ "status" ].asInt() == 2 );
		const auto returned_hash { statuses[ 0 ][ "hash" ].asString() };
		CHECK( returned_hash.size() == 64 );
		if ( hash.empty() ) hash = returned_hash;

		CHECK( returned_hash == hash );
	}
	const auto hash_only_record { api().createRecord( 98765 ) };
	Json::Value associated { Json::objectValue };
	associated[ "urls" ] = Json::Value { Json::arrayValue };
	associated[ "urls" ].append( post_url );
	REQUIRE( api().post( std::format( "/records/{}/urls/add", hash_only_record ), associated ).status == 200 );
	const auto stored_only { api().get(
		"/hyapi/add_urls/get_url_files", { { "url", post_url }, { "Hydrus-Client-API-Access-Key", api().key() } } ) };
	REQUIRE( stored_only.status == 200 );
	REQUIRE( stored_only.json[ "url_file_statuses" ].size() == 1 );
	CHECK( stored_only.json[ "url_file_statuses" ][ 0 ][ "hash" ].asString() == hash );

	REQUIRE( api().del( std::format( "/download_sessions/{}", session_id ) ).status == 200 );
}

TEST_CASE_METHOD( ServerFixture, "Hydrus URL routes reject invalid requests", "[api][hyapi]" )
{
	const QueryParams auth { { "Hydrus-Client-API-Access-Key", api().key() } };
	CHECK( api().get( "/hyapi/add_urls/get_url_files", auth ).status == 400 );
	CHECK(
		api()
			.get( "/hyapi/add_urls/get_url_files", { { "url", "" }, { "Hydrus-Client-API-Access-Key", api().key() } } )
			.status
	    == 400 );
	CHECK( api().post( "/hyapi/add_urls/add_url", Json::Value { Json::objectValue }, auth ).status == 400 );
	for ( const auto* url : { "", "not a URL", "file:///etc/hosts", "ftp://example.com/file" } )
	{
		Json::Value body { Json::objectValue };
		body[ "url" ] = url;
		CHECK( api().post( "/hyapi/add_urls/add_url", body, auth ).status == 400 );
	}
	CHECK( api().get( "/download_sessions" ).json.empty() );
}

} // namespace idhan::test
