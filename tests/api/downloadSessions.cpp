#include <arpa/inet.h>
#include <catch2/catch_test_macros.hpp>
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
#include <unistd.h>

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
				     && std::ranges::equal( name, std::string_view { "cookie" }, []( const char left, const char right )
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
				0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x01, 0x00, 0x01, 0x00, 0x80,
				0xff, 0x00, 0xc0, 0xc0, 0xc0, 0x00, 0x00, 0x00, 0x21, 0xf9, 0x04,
				0x01, 0x00, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x00, 0x01,
				0x00, 0x01, 0x00, 0x00, 0x02, 0x02, 0x44, 0x01, 0x00, 0x3b,
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
		if ( m_socket >= 0 )
		{
			::shutdown( m_socket, SHUT_RDWR );
			::close( m_socket );
		}
	}

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
			tx.exec( "DELETE FROM file_clusters WHERE folder_path = $1", pqxx::params { m_path.string() } );
			tx.commit();
		}
		catch ( const std::exception& e )
		{
			WARN( "Failed to clean up the download session cluster: " << e.what() );
		}
	}

	void registerWith( ApiClient& api ) const
	{
		std::filesystem::create_directories( m_path );

		Json::Value body {};
		body[ "name" ] = "download session imports";
		body[ "path" ] = m_path.string();
		body[ "readonly" ] = false;

		const auto added { api.post( "/clusters/add", body ) };
		INFO( added.body );
		REQUIRE( added.status == drogon::k200OK );
	}
};

Json::Value waitForDownloadJob( ApiClient& api, const DownloadSessionID session_id, const DownloadSessionUrlID job_id )
{
	const auto deadline { std::chrono::steady_clock::now() + wait_deadline };

	while ( std::chrono::steady_clock::now() < deadline )
	{
		const auto listed { api.get( std::format( "/download_sessions/{}/urls", session_id ) ) };
		if ( listed.status != drogon::k200OK )
			throw std::runtime_error( "Could not list download jobs: " + listed.body );

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
		if ( listed.status != drogon::k200OK )
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
	REQUIRE( created.status == drogon::k201Created );
	const auto session_id { created.json[ "id" ].as< DownloadSessionID >() };

	Json::Value url_body {};
	url_body[ "url" ] = fixture.url( "/gallery" );
	const auto first { api().post( std::format( "/download_sessions/{}/urls", session_id ), url_body ) };
	REQUIRE( first.status == drogon::k201Created );
	CHECK( first.json[ "state" ].asString() == "pending" );

	const auto first_job { waitForDownloadJob(
		api(), session_id, first.json[ "id" ].as< DownloadSessionUrlID >() ) };
	CHECK( first_job[ "state" ].asString() == "completed" );
	CHECK_FALSE( first_job[ "finished_at" ].isNull() );
	CHECK( first_job[ "error" ].isNull() );
	CHECK( fixture.cookieHeader().contains( "fringeBenefits=yup" ) );
	const auto flattened { waitForFlattenedRoot(
		api(), session_id, first.json[ "id" ].as< DownloadSessionUrlID >() ) };
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

	const auto records { api().get( std::format( "/download_sessions/{}/records", session_id ) ) };
	REQUIRE( records.status == drogon::k200OK );
	REQUIRE( records.json[ "record_ids" ].isArray() );
	CHECK( records.json[ "record_ids" ].size() == 1 );

	url_body[ "url" ] = fixture.url( "/failure" );
	const auto failing { api().post( std::format( "/download_sessions/{}/urls", session_id ), url_body ) };
	REQUIRE( failing.status == drogon::k201Created );
	const auto failed_job { waitForDownloadJob(
		api(), session_id, failing.json[ "id" ].as< DownloadSessionUrlID >() ) };
	CHECK( failed_job[ "state" ].asString() == "failed" );
	CHECK( failed_job[ "error" ].asString().contains( "deterministic parser failure" ) );

	url_body[ "url" ] = fixture.url( "/unmatched" );
	const auto unmatched { api().post( std::format( "/download_sessions/{}/urls", session_id ), url_body ) };
	CHECK( unmatched.status == drogon::k400BadRequest );
	const auto jobs { api().get( std::format( "/download_sessions/{}/urls", session_id ) ) };
	REQUIRE( jobs.status == drogon::k200OK );
	CHECK( jobs.json.size() == 2 );
}

} // namespace idhan::test
