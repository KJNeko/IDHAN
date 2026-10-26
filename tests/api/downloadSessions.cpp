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

			const std::string body { responseBody( path ) };
			const std::string response { std::format(
				"HTTP/1.1 {}\r\nContent-Type: {}\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}",
				body.empty() ? "404 Not Found" : "200 OK",
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
			const auto state { job[ "state" ].asString() };
			if ( state == "completed" || state == "failed" ) return job;
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

TEST_CASE_METHOD(
	ServerFixture,
	"Downloader debug SSE pushes runtime state and session removal without REST polling",
	"[api][download-sessions][debug-events]" )
{
	DownloadSessionHttpFixture fixture;
	JobEventStream events { serverPort(), api().key(), "/events?types=downloader.debug" };
	const auto initial { events.waitFor( "snapshot" ) };
	REQUIRE( initial[ "sessions" ].isArray() );
	REQUIRE( initial[ "lanes" ].isArray() );

	Json::Value body;
	body[ "name" ] = "streamed diagnostics";
	const auto created { api().post( "/download_sessions", body ) };
	REQUIRE( created.status == 201 );
	const auto id { created.json[ "id" ].as< DownloadSessionID >() };
	body = Json::Value {};
	body[ "url" ] = fixture.url( "/failure" );
	REQUIRE( api().post( std::format( "/download_sessions/{}/urls", id ), body ).status == 201 );

	bool failed {};
	bool saw_failure_event {};
	for ( int attempt {}; attempt < 5 && !failed; ++attempt )
	{
		const auto update { events.waitFor( "update" ) };
		REQUIRE( update[ "sessions" ].isArray() );
		for ( const auto& session : update[ "sessions" ] )
		{
			if ( session[ "id" ].as< DownloadSessionID >() != id ) continue;

			CHECK( session[ "name" ].asString() == "streamed diagnostics" );
			failed = session[ "counters" ][ "work_failed" ].asUInt64() > 0;
			for ( const auto& event : session[ "events" ] )
				if ( event[ "kind" ].asString() == "failed" ) saw_failure_event = true;
		}
	}
	REQUIRE( failed );
	CHECK( saw_failure_event );
	REQUIRE( api().del( std::format( "/download_sessions/{}", id ) ).status == 200 );
	bool removed {};
	for ( int attempt {}; attempt < 5 && !removed; ++attempt )
	{
		const auto update { events.waitFor( "update" ) };
		REQUIRE( update[ "sessions" ].isArray() );
		removed = true;
		for ( const auto& session : update[ "sessions" ] )
			if ( session[ "id" ].as< DownloadSessionID >() == id ) removed = false;
	}
	CHECK( removed );
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
	url_body[ "url" ] = fixture.url( "/post" );
	const auto submitted { api().post( std::format( "/download_sessions/{}/urls", session_id ), url_body ) };
	REQUIRE( submitted.status == 201 );
	const auto imported {
		waitForImportedDownload( api(), session_id, submitted.json[ "id" ].as< DownloadSessionUrlID >() )
	};
	const auto record_id { imported[ "record_id" ].as< RecordID >() };
	CHECK( fixture.fileRequests() == 1 );

	url_body[ "url" ] = fixture.url( "/post?domain=fixture-override" );
	const auto repeated { api().post( std::format( "/download_sessions/{}/urls", session_id ), url_body ) };
	REQUIRE( repeated.status == 201 );
	const auto reused {
		waitForImportedDownload( api(), session_id, repeated.json[ "id" ].as< DownloadSessionUrlID >() )
	};
	CHECK( reused[ "record_id" ].as< RecordID >() == record_id );
	CHECK( fixture.fileRequests() == 1 );

	url_body[ "url" ] = fixture.url( "/post?multipleDomains=1" );
	const auto multi { api().post( std::format( "/download_sessions/{}/urls", session_id ), url_body ) };
	REQUIRE( multi.status == 201 );
	const auto grouped {
		waitForImportedDownload( api(), session_id, multi.json[ "id" ].as< DownloadSessionUrlID >() )
	};
	CHECK( grouped[ "record_id" ].as< RecordID >() == record_id );
	CHECK( fixture.fileRequests() == 1 );

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
		if ( url[ "note" ].asString() == "Already imported earlier in this session" ) ++session_duplicates;
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
			"JOIN download_session_urls parent ON parent.download_session_url_id = child.parent_url_id "
			"WHERE child.download_session_id = $1 AND child.state = 'pending' "
			"AND parent.state = 'completed' AND child.url = $2",
			pqxx::params { session_id, fixture.url( "/file.gif" ) } );
		if ( persisted ) break;
		std::this_thread::sleep_for( 25ms );
	}
	REQUIRE( persisted );

	testServer().restart( crash );
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

} // namespace idhan::test
