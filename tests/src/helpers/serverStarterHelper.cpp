//
// Created by kj16609 on 7/15/25.
//

#include "serverStarterHelper.hpp"

#include <spdlog/spdlog.h>
#include <sys/prctl.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <signal.h>
#include <unistd.h>

[[nodiscard]] ServerHandle startServer()
{
	constexpr std::string_view executable_name { "IDHANServer" };

	// Search relative to the test binary's own location
	std::filesystem::path executable;
	{
		std::array< char, 4096 > buf {};
		const auto len { readlink( "/proc/self/exe", buf.data(), buf.size() - 1 ) };
		if ( len > 0 )
		{
			buf[ len ] = '\0';
			executable = std::filesystem::path( buf.data() ).parent_path() / executable_name;
		}
	}

	if ( executable.empty() || !std::filesystem::exists( executable ) )
	{
		// Fallback: search cwd
		executable = std::filesystem::current_path() / executable_name;
	}

	if ( !std::filesystem::exists( executable ) )
		throw std::runtime_error(
			std::format( "IDHANServer executable does not exist. Searched {}", executable.string() ) );

	const std::array< char*, 5 > args {
		const_cast< char* >( executable.c_str() ), "--testmode=1", "--use_stdout=1", "--log_level=debug", nullptr
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
