//
// Created by kj16609 on 2/21/25.
//

// When testing the server binary should be in the same folder as the test binary. As a result, We can start it outselves

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <signal.h>

#include "spdlog/spdlog.h"

struct ServerHandle
{
	pid_t pid;

	~ServerHandle()
	{
		kill( pid, SIGHUP );
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
};

[[nodiscard]] inline ServerHandle startServer()
{
	const pid_t pid { fork() };
	if ( pid == 0 )
	{
		// we are the child
		std::system( "./IDHANServer --testmode" );
		std::cout << "Started server" << std::endl;
	}
	else if ( pid > 0 )
	{
		// we are the parent
		return { pid };
	}
	else
	{
		throw std::runtime_error( "Failed to start server" );
	}

	using namespace std::chrono_literals;

	std::this_thread::sleep_for( 500ms );
}
