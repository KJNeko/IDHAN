//
// Created by kj16609 on 2/21/25.
//

// When testing the server binary should be in the same folder as the test binary. As a result, We can start it outselves
#pragma once

#include <sys/prctl.h>

#include <chrono>
#include <cstdlib>
#include <signal.h>
#include <unistd.h>

struct ServerHandle
{
	// pid_t pid;
	// pid_t monitor_pid;

	/*
	~ServerHandle()
	{
		spdlog::debug( "Killing server instance" );
		kill( pid, SIGHUP );
		kill( monitor_pid, SIGKILL );
		std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
		if ( kill( pid, 0 ) == 0 )
		{
			kill( pid, SIGINT );
			std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
			// still alive?
			if ( kill( pid, 0 ) == 0 )
			{
				kill( pid, SIGKILL );
			}
		}
	}
	*/
};

/*
inline void monitor( const int server_pid, const int test_pid )
{
	// if the test pid doesn't exist, then we need to terminate the server pid if it also exists still

	const auto timeout { std::chrono::seconds( 1 ) };

	spdlog::info( "Monitor watching server {}, test {}", server_pid, test_pid );

	while ( true )
	{
		std::this_thread::sleep_for( timeout );

		// test if the test_pid is still in use/valid
		if ( kill( test_pid, 0 ) == -1 )
		{
			spdlog::critical( "Monitor detected test PID terminated without server PID terminated" );

			spdlog::debug( "Killing server instance" );
			kill( server_pid, SIGHUP );
			std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
			if ( kill( server_pid, 0 ) == 0 )
			{
				kill( server_pid, SIGINT );
				std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
				// still alive?
				if ( kill( server_pid, 0 ) == 0 )
				{
					kill( server_pid, SIGKILL );
				}
			}

			return;
		}
	}
}
*/

[[nodiscard]] inline ServerHandle startServer()
{
	constexpr std::string_view executable_name { "IDHANServer" };
	const std::filesystem::path current_dir { std::filesystem::current_path() };
	const auto executable { current_dir / executable_name };
	if ( !std::filesystem::exists( executable ) ) throw std::runtime_error( "IDHANServer executable does not exist" );

	const std::array< char*, 4 > args {
		const_cast< char* >( executable.c_str() ), "--testmode", "--use_stdout=1", nullptr
	};

	if ( const pid_t server_pid = fork(); server_pid == 0 )
	{
		// we are the child
		spdlog::info( "Opened server process" );
		prctl( PR_SET_PDEATHSIG, SIGTERM ); // terminate if the parent also terminates
		execv( executable.string().c_str(), args.data() );

		spdlog::critical( "Failed to open server process" );
		exit( EXIT_FAILURE );
	}

	std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );

	return {};
}

#define SERVER_HANDLE const auto _ { startServer() };
