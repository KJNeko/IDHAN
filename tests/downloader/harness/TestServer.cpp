#include "TestServer.hpp"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>
#include <format>
#include <stdexcept>

namespace idhan::test
{

static std::string lowered( std::string value )
{
	std::ranges::transform(
		value,
		value.begin(),
		[]( const unsigned char character ) { return static_cast< char >( std::tolower( character ) ); } );
	return value;
}

TestServer::TestServer()
{
	m_listen = ::socket( AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0 );

	if ( m_listen < 0 ) throw std::runtime_error( "test server: socket failed" );

	int reuse { 1 };
	::setsockopt( m_listen, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );

	sockaddr_in address {};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = ::htonl( INADDR_LOOPBACK );
	address.sin_port = 0;

	if ( ::bind( m_listen, reinterpret_cast< sockaddr* >( &address ), sizeof( address ) ) != 0 )
		throw std::runtime_error( "test server: bind failed" );

	if ( ::listen( m_listen, 128 ) != 0 ) throw std::runtime_error( "test server: listen failed" );

	socklen_t length { sizeof( address ) };

	if ( ::getsockname( m_listen, reinterpret_cast< sockaddr* >( &address ), &length ) != 0 )
		throw std::runtime_error( "test server: getsockname failed" );

	m_port = ::ntohs( address.sin_port );
	m_acceptor = std::jthread( [ this ] { accept(); } );
}

TestServer::~TestServer()
{
	stop();

	if ( m_acceptor.joinable() ) m_acceptor.join();

	for ( auto& worker : m_workers )
	{
		if ( worker.joinable() ) worker.join();
	}
}

void TestServer::stop()
{
	if ( !m_running.exchange( false ) ) return;

	::shutdown( m_listen, SHUT_RDWR );
	::close( m_listen );
	m_listen = -1;
}

std::string TestServer::url( const std::string_view path ) const
{
	return std::format( "http://127.0.0.1:{}{}", m_port, path );
}

void TestServer::route( std::string path, Handler handler )
{
	const std::scoped_lock lock { m_mutex };
	m_routes.insert_or_assign( std::move( path ), std::move( handler ) );
}

std::vector< TestRequest > TestServer::requests() const
{
	const std::scoped_lock lock { m_mutex };
	return m_seen;
}

std::size_t TestServer::requestCount() const
{
	const std::scoped_lock lock { m_mutex };
	return m_seen.size();
}

void TestServer::accept()
{
	while ( m_running.load() )
	{
		const int client { ::accept4( m_listen, nullptr, nullptr, SOCK_CLOEXEC ) };

		if ( client < 0 ) break;

		int nodelay { 1 };
		::setsockopt( client, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof( nodelay ) );
		m_accepted_connections.fetch_add( 1 );
		m_open_connections.fetch_add( 1 );
		m_workers.emplace_back( [ this, client ] { serve( client ); } );
	}
}

static bool readLine( const int socket, std::string& buffer, std::string& line )
{
	for ( ;; )
	{
		if ( const auto end { buffer.find( "\r\n" ) }; end != std::string::npos )
		{
			line = buffer.substr( 0, end );
			buffer.erase( 0, end + 2 );
			return true;
		}

		char chunk[ 4096 ] {};
		const auto read_bytes { ::recv( socket, chunk, sizeof( chunk ), 0 ) };

		if ( read_bytes <= 0 ) return false;

		buffer.append( chunk, static_cast< std::size_t >( read_bytes ) );
	}
}

static bool readExactly( const int socket, std::string& buffer, const std::size_t size, std::string& out )
{
	while ( buffer.size() < size )
	{
		char chunk[ 4096 ] {};
		const auto read_bytes { ::recv( socket, chunk, sizeof( chunk ), 0 ) };

		if ( read_bytes <= 0 ) return false;

		buffer.append( chunk, static_cast< std::size_t >( read_bytes ) );
	}

	out = buffer.substr( 0, size );
	buffer.erase( 0, size );
	return true;
}

static bool writeAll( const int socket, const std::string_view data )
{
	std::size_t sent {};

	while ( sent < data.size() )
	{
		const auto written { ::send( socket, data.data() + sent, data.size() - sent, MSG_NOSIGNAL ) };

		if ( written <= 0 ) return false;

		sent += static_cast< std::size_t >( written );
	}

	return true;
}

void TestServer::serve( const int socket )
{
	std::string buffer {};

	while ( m_running.load() )
	{
		std::string line {};

		if ( !readLine( socket, buffer, line ) ) break;

		TestRequest request {};
		const auto first_space { line.find( ' ' ) };
		const auto second_space { line.find( ' ', first_space + 1 ) };

		if ( first_space == std::string::npos || second_space == std::string::npos ) break;

		request.method = line.substr( 0, first_space );
		request.target = line.substr( first_space + 1, second_space - first_space - 1 );
		request.path = request.target.substr( 0, request.target.find( '?' ) );

		for ( ;; )
		{
			if ( !readLine( socket, buffer, line ) ) return;
			if ( line.empty() ) break;

			const auto separator { line.find( ':' ) };

			if ( separator == std::string::npos ) continue;

			std::string value { line.substr( separator + 1 ) };

			while ( !value.empty() && ( value.front() == ' ' || value.front() == '\t' ) ) value.erase( value.begin() );

			request.headers.emplace( lowered( line.substr( 0, separator ) ), std::move( value ) );
		}

		if ( const auto length { request.headers.find( "content-length" ) }; length != request.headers.end() )
		{
			std::size_t size {};
			std::from_chars( length->second.data(), length->second.data() + length->second.size(), size );

			if ( size != 0 && !readExactly( socket, buffer, size, request.body ) ) return;
		}

		Handler handler {};

		{
			const std::scoped_lock lock { m_mutex };
			m_seen.emplace_back( request );

			if ( const auto found { m_routes.find( request.path ) }; found != m_routes.end() ) handler = found->second;
		}

		TestResponse response { handler ? handler( request )
		                                : TestResponse { .status = 404, .reason = "Not Found", .body = "missing" } };

		std::string out { std::format( "HTTP/1.1 {} {}\r\n", response.status, response.reason ) };

		for ( const auto& [ name, value ] : response.headers ) out += std::format( "{}: {}\r\n", name, value );

		out += std::format( "Content-Length: {}\r\n", response.body.size() );
		out += "Connection: keep-alive\r\n\r\n";

		if ( request.method != "HEAD" ) out += response.body;

		if ( !writeAll( socket, out ) ) break;
	}

	m_open_connections.fetch_sub( 1 );
	::close( socket );
}

} // namespace idhan::test
