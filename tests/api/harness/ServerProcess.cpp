#include "ServerProcess.hpp"

#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
#include <format>
#include <stdexcept>
#include <thread>
#include <vector>

#include "../../common/TestConnection.hpp"

extern char** environ;

namespace idhan::test
{

std::uint16_t findFreePort()
{
	const int socket_fd { ::socket( AF_INET, SOCK_STREAM, 0 ) };
	if ( socket_fd < 0 ) throw std::runtime_error( "Could not open a socket to look for a free port" );

	sockaddr_in address {};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
	address.sin_port = 0;

	if ( ::bind( socket_fd, reinterpret_cast< sockaddr* >( &address ), sizeof( address ) ) != 0 )
	{
		::close( socket_fd );
		throw std::runtime_error( "Could not bind a socket to look for a free port" );
	}

	socklen_t length { sizeof( address ) };
	if ( ::getsockname( socket_fd, reinterpret_cast< sockaddr* >( &address ), &length ) != 0 )
	{
		::close( socket_fd );
		throw std::runtime_error( "Could not read back the port the kernel picked" );
	}

	::close( socket_fd );

	return ntohs( address.sin_port );
}

//! The server binary sits beside the test binary, unless IDHAN_TEST_SERVER_BINARY names another one.
static std::filesystem::path serverExecutable()
{
	if ( const auto configured { envOr( "IDHAN_TEST_SERVER_BINARY", "" ) }; !configured.empty() )
	{
		if ( !std::filesystem::exists( configured ) )
			throw std::runtime_error( std::format( "IDHAN_TEST_SERVER_BINARY names {}, which does not exist", configured ) );

		return configured;
	}

	std::array< char, 4096 > buffer {};
	const auto length { ::readlink( "/proc/self/exe", buffer.data(), buffer.size() - 1 ) };

	if ( length <= 0 ) throw std::runtime_error( "Could not resolve the test binary's own path" );

	const std::filesystem::path executable {
		std::filesystem::path( std::string_view( buffer.data(), static_cast< std::size_t >( length ) ) ).parent_path()
		/ "IDHANServer"
	};

	if ( !std::filesystem::exists( executable ) )
		throw std::runtime_error(
			std::format( "IDHANServer does not exist at {}. Build it before running the API tests", executable.string() ) );

	return executable;
}

static bool respondsToHealth( const std::uint16_t port )
{
	const int socket_fd { ::socket( AF_INET, SOCK_STREAM, 0 ) };
	if ( socket_fd < 0 ) return false;

	sockaddr_in address {};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
	address.sin_port = htons( port );

	const bool connected { ::connect( socket_fd, reinterpret_cast< sockaddr* >( &address ), sizeof( address ) ) == 0 };

	if ( !connected )
	{
		::close( socket_fd );
		return false;
	}

	const std::string request { std::format( "GET /health HTTP/1.1\r\nHost: 127.0.0.1:{}\r\n\r\n", port ) };
	const bool sent { ::send( socket_fd, request.data(), request.size(), MSG_NOSIGNAL )
		              == static_cast< ssize_t >( request.size() ) };

	std::array< char, 64 > response {};
	const auto received { sent ? ::recv( socket_fd, response.data(), response.size() - 1, 0 ) : -1 };

	::close( socket_fd );

	return received > 0 && std::string_view( response.data(), static_cast< std::size_t >( received ) ).contains( "200" );
}

ServerProcess::ServerProcess( std::string schema ) :
  m_temp_path( std::filesystem::temp_directory_path() / std::format( "idhan-api-tests-{}", ::getpid() ) ),
  m_schema( std::move( schema ) ),
  m_port( findFreePort() )
{
	std::filesystem::remove_all( m_temp_path );
	std::filesystem::create_directories( m_temp_path );

	const auto executable { serverExecutable() };
	const PostgresSettings postgres {};

	std::vector< std::string > settings {
		std::format( "IDHAN_DATABASE_HOST={}", postgres.host ),
		std::format( "IDHAN_DATABASE_PORT={}", postgres.port ),
		std::format( "IDHAN_DATABASE_DATABASE={}", postgres.database ),
		std::format( "IDHAN_DATABASE_USER={}", postgres.user ),
		std::format( "IDHAN_DATABASE_PASSWORD={}", postgres.password ),
		std::format( "IDHAN_DATABASE_SCHEMA={}", m_schema ),
		std::format( "IDHAN_SERVER_PORT={}", m_port ),
		// the pool is one postgres connection per io thread, and the default fills the server's shared limit
		std::format( "IDHAN_SERVER_IO_THREADS={}", envOr( "IDHAN_TEST_SERVER_IO_THREADS", "4" ) ),
		std::format( "IDHAN_SERVER_TEMP_PATH={}", m_temp_path.string() ),
		// the default is ./log, which would be the directory the tests were started from
		std::format( "IDHAN_LOGGING_PATH={}", ( m_temp_path / "log" ).string() ),
		std::format( "IDHAN_HOST_IPV4_LISTEN={}", "127.0.0.1" ),
		std::format( "IDHAN_HOST_IPV6_LISTEN={}", "" ),
		std::format( "IDHAN_LOGGING_LEVEL={}", envOr( "IDHAN_TEST_SERVER_LOG_LEVEL", "warning" ) )
	};

	std::vector< char* > environment {};
	for ( char** entry = environ; *entry != nullptr; ++entry ) environment.emplace_back( *entry );
	for ( auto& setting : settings ) environment.emplace_back( setting.data() );
	environment.emplace_back( nullptr );

	const auto log_path { m_log_path.string() };
	const bool inherit_output { !envOr( "IDHAN_TEST_SERVER_OUTPUT", "" ).empty() };

	const auto executable_string { executable.string() };
	std::string log_flag { "--use_stdout=1" };
	std::array< char*, 3 > arguments { { const_cast< char* >( executable_string.c_str() ), log_flag.data(), nullptr } };

	m_pid = ::fork();

	if ( m_pid == 0 )
	{
		::prctl( PR_SET_PDEATHSIG, SIGTERM );

		if ( !inherit_output )
		{
			const int log_fd { ::open( log_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644 ) };
			if ( log_fd < 0 ) ::_exit( EXIT_FAILURE );
			if ( ::dup2( log_fd, STDOUT_FILENO ) < 0 || ::dup2( log_fd, STDERR_FILENO ) < 0 ) ::_exit( EXIT_FAILURE );
			::close( log_fd );
		}

		::execve( executable_string.c_str(), arguments.data(), environment.data() );
		::_exit( EXIT_FAILURE );
	}

	if ( m_pid < 0 ) throw std::runtime_error( "Could not fork a server process" );

	waitUntilReady();
}

std::string ServerProcess::logTail() const
{
	std::ifstream log { m_log_path };
	if ( !log ) return "(the server wrote no log)";

	const std::string contents { std::istreambuf_iterator< char > { log }, std::istreambuf_iterator< char > {} };

	constexpr std::size_t wanted_lines { 40 };
	auto start { contents.size() };

	for ( std::size_t seen { 0 }; start > 0 && seen < wanted_lines; --start )
		if ( contents[ start - 1 ] == '\n' ) ++seen;

	return contents.substr( start );
}

void ServerProcess::waitUntilReady() const
{
	constexpr auto timeout { std::chrono::seconds( 60 ) };
	const auto deadline { std::chrono::steady_clock::now() + timeout };

	while ( std::chrono::steady_clock::now() < deadline )
	{
		int status {};
		if ( ::waitpid( m_pid, &status, WNOHANG ) == m_pid )
			throw std::runtime_error(
				std::format( "The server exited during startup with status {}:\n{}", status, logTail() ) );

		if ( respondsToHealth( m_port ) ) return;

		std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
	}

	::kill( m_pid, SIGKILL );
	int status {};
	::waitpid( m_pid, &status, 0 );

	throw std::runtime_error(
		std::format( "The server never answered /health on port {}:\n{}", m_port, logTail() ) );
}

ServerProcess::~ServerProcess()
{
	stop();

	std::error_code error {};
	std::filesystem::remove_all( m_temp_path, error );
}

void ServerProcess::stop()
{
	if ( m_pid > 0 )
	{
		::kill( m_pid, SIGTERM );

		const auto deadline { std::chrono::steady_clock::now() + std::chrono::seconds( 10 ) };
		bool reaped { false };

		while ( !reaped && std::chrono::steady_clock::now() < deadline )
		{
			int status {};
			reaped = ::waitpid( m_pid, &status, WNOHANG ) == m_pid;
			if ( !reaped ) std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
		}

		if ( !reaped )
		{
			::kill( m_pid, SIGKILL );
			int status {};
			::waitpid( m_pid, &status, 0 );
		}

		m_pid = -1;
	}
}

} // namespace idhan::test
