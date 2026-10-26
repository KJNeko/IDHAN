#include <arpa/inet.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <sys/socket.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <chrono>
#include <cstring>
#include <format>
#include <future>
#include <stdexcept>
#include <thread>
#include <unistd.h>

#include "ApiClient.hpp"
#include "ServerProcess.hpp"

namespace idhan::test
{

class ClientTransportServer
{
	int m_listener { -1 };
	std::uint16_t m_port {};
	std::string m_reply;
	std::chrono::milliseconds m_delay;
	std::promise< std::string > m_request;
	std::jthread m_worker;

	static std::size_t contentLength( std::string headers )
	{
		std::ranges::transform(
			headers, headers.begin(), []( const unsigned char character ) { return std::tolower( character ); } );
		const auto begin { headers.find( "\r\ncontent-length:" ) };
		if ( begin == std::string::npos ) return 0;


		auto value { std::string_view { headers }.substr( begin + std::strlen( "\r\ncontent-length:" ) ) };
		while ( !value.empty() && value.front() == ' ' ) value.remove_prefix( 1 );
		std::size_t length {};
		const auto parsed { std::from_chars( value.data(), value.data() + value.size(), length ) };
		if ( parsed.ec != std::errc {} ) throw std::runtime_error( "Invalid request content length" );


		return length;
	}

	void serve() noexcept
	{
		const int connection { ::accept( m_listener, nullptr, nullptr ) };
		try
		{
			if ( connection < 0 ) throw std::runtime_error( "Transport test accept failed" );


			const timeval timeout { 3, 0 };
			::setsockopt( connection, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof( timeout ) );
			std::string request;
			std::array< char, 4096 > buffer {};
			for ( ;; )
			{
				const auto received { ::recv( connection, buffer.data(), buffer.size(), 0 ) };
				if ( received <= 0 ) throw std::runtime_error( "Incomplete transport test request" );
				request.append( buffer.data(), static_cast< std::size_t >( received ) );
				const auto end { request.find( "\r\n\r\n" ) };
				if ( end == std::string::npos ) continue;
				if ( request.size() >= end + 4 + contentLength( request.substr( 0, end ) ) ) break;
			}
			m_request.set_value( std::move( request ) );
		}
		catch ( ... )
		{
			m_request.set_exception( std::current_exception() );
			if ( connection >= 0 ) ::close( connection );
			return;
		}
		std::this_thread::sleep_for( m_delay );
		std::size_t offset {};
		while ( offset < m_reply.size() )
		{
			const auto sent { ::send( connection, m_reply.data() + offset, m_reply.size() - offset, MSG_NOSIGNAL ) };
			if ( sent <= 0 ) break;
			offset += static_cast< std::size_t >( sent );
		}
		::close( connection );
	}

  public:

	ClientTransportServer( std::string reply, const std::chrono::milliseconds delay = {} ) :
	  m_reply( std::move( reply ) ),
	  m_delay( delay )
	{
		m_listener = ::socket( AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0 );
		if ( m_listener < 0 ) throw std::runtime_error( "Transport test socket failed" );
		sockaddr_in address {};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
		if ( ::bind( m_listener, reinterpret_cast< sockaddr* >( &address ), sizeof( address ) ) != 0
		     || ::listen( m_listener, 1 ) != 0 )
		{
			::close( m_listener );
			throw std::runtime_error( "Transport test listen failed" );
		}
		socklen_t length { sizeof( address ) };
		if ( ::getsockname( m_listener, reinterpret_cast< sockaddr* >( &address ), &length ) != 0 )
		{
			::close( m_listener );
			throw std::runtime_error( "Transport test address lookup failed" );
		}
		m_port = ntohs( address.sin_port );
		m_worker = std::jthread { &ClientTransportServer::serve, this };
	}

	~ClientTransportServer()
	{
		::shutdown( m_listener, SHUT_RDWR );
		m_worker.join();
		::close( m_listener );
	}

	std::uint16_t port() const { return m_port; }

	std::string request() { return m_request.get_future().get(); }
};

static std::string transportReply( const std::string_view body = "{}", const std::string_view headers = "" )
{
	return std::format(
		"HTTP/1.1 200 OK\r\nContent-Length: {}\r\n{}Connection: close\r\n\r\n{}", body.size(), headers, body );
}

TEST_CASE( "API transport preserves repeated and escaped query values", "[api][transport]" )
{
	ClientTransportServer server {
		transportReply( "[]", "cOnTeNt-TyPe: application/json\r\nX-Custom:  keep me \t\r\n" )
	};
	ApiClient client { server.port() };
	const auto response { client.get(
		"/query",
		{ { "tag_domains", "2" }, { "tag_domains", "3" }, { "text", "a b+&%=\xc3\xa9" }, { "empty", "" } } ) };
	CHECK( response.status == 200 );
	CHECK( response.json.isArray() );
	CHECK( response.header( "content-type" ) == "application/json" );
	CHECK( response.header( "x-custom" ) == "keep me" );
	CHECK( response.header( "absent" ).empty() );
	CHECK( server.request().starts_with(
		"GET /query?tag_domains=2&tag_domains=3&text=a%20b%2B%26%25%3D%C3%A9&empty= HTTP/1.1\r\n" ) );
}

TEST_CASE( "API transport preserves method, authentication, JSON and raw bytes", "[api][transport]" )
{
	SECTION( "JSON and credentials" )
	{
		ClientTransportServer server { transportReply( "{\"ok\":true}" ) };
		ApiClient client { server.port() };
		Json::Value body;
		body[ "text" ] = std::string { "a\0b", 3 };
		const auto response { client.postWithKey( "/json", body, "test-key" ) };
		CHECK( response.json[ "ok" ].asBool() );
		const auto request { server.request() };
		CHECK( request.starts_with( "POST /json HTTP/1.1\r\n" ) );
		CHECK( request.contains( "X-API-Key: test-key\r\n" ) );
		CHECK( request.contains( "Content-Type: application/json\r\n" ) );
		CHECK( request.ends_with( "{\"text\":\"a\\u0000b\"}" ) );
	}
	SECTION( "binary upload and chunked binary response" )
	{
		const std::string binary { "a\0\xff", 3 };
		ClientTransportServer server {
			"HTTP/1.1 100 Continue\r\nX-Interim: ignored\r\n\r\nHTTP/1.1 201 Created\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n3\r\n"
			+ binary + "\r\n0\r\n\r\n"
		};
		ApiClient client { server.port() };
		const auto response { client.postOctets( "/binary", binary ) };
		CHECK( response.status == 201 );
		CHECK( response.body == binary );
		CHECK( response.json.isNull() );
		CHECK( response.header( "x-interim" ).empty() );
		const auto request { server.request() };
		CHECK( request.contains( "Content-Type: application/octet-stream\r\n" ) );
		CHECK( request.ends_with( binary ) );
	}
	SECTION( "PATCH" )
	{
		ClientTransportServer server { transportReply() };
		ApiClient client { server.port() };
		CHECK( client.patch( "/patch", Json::Value { Json::objectValue } ).status == 200 );
		CHECK( server.request().starts_with( "PATCH /patch HTTP/1.1\r\n" ) );
	}
	SECTION( "DELETE" )
	{
		ClientTransportServer server { transportReply() };
		ApiClient client { server.port() };
		CHECK( client.del( "/delete" ).status == 200 );
		CHECK( server.request().starts_with( "DELETE /delete HTTP/1.1\r\n" ) );
	}
	SECTION( "bodyless POST" )
	{
		ClientTransportServer server { transportReply() };
		ApiClient client { server.port() };
		CHECK( client.postWithoutBody( "/empty" ).status == 200 );
		CHECK( server.request().ends_with( "\r\n\r\n" ) );
	}
}

TEST_CASE( "API transport reports HTTP errors and redirects without following them", "[api][transport]" )
{
	for ( const auto status : { 302, 400, 503 } )
	{
		ClientTransportServer server { std::format(
			"HTTP/1.1 {} Test\r\nLocation: /unexpected\r\nContent-Length: 4\r\nConnection: close\r\n\r\noops",
			status ) };
		ApiClient client { server.port() };
		const auto response { client.get( "/status" ) };
		CHECK( response.status == status );
		CHECK( response.body == "oops" );
		CHECK( response.json.isNull() );
		CHECK( response.header( "location" ) == "/unexpected" );
	}
}

TEST_CASE( "API transport has bounded deadlines and connection failure diagnostics", "[api][transport]" )
{
	SECTION( "deadline" )
	{
		ClientTransportServer server { transportReply(), std::chrono::milliseconds { 1500 } };
		ApiClient client { server.port(), 1 };
		CHECK_THROWS_WITH( client.get( "/slow" ), Catch::Matchers::ContainsSubstring( "Request to /slow failed:" ) );
	}
	SECTION( "upload deadline" )
	{
		ClientTransportServer server { transportReply(), std::chrono::milliseconds { 1500 } };
		ApiClient client { server.port(), 30, 1 };
		CHECK_THROWS_WITH( client.postOctets( "/slow-upload", "bytes" ), Catch::Matchers::ContainsSubstring( "Request to /slow-upload failed:" ) );
	}
	SECTION( "connection refused" )
	{
		ApiClient client { findFreePort() };
		CHECK_THROWS_WITH(
			client.get( "/offline" ), Catch::Matchers::ContainsSubstring( "Request to /offline failed:" ) );
	}
}

} // namespace idhan::test
