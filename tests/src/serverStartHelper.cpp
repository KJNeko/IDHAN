//
// Created by kj16609 on 7/15/25.
//

#include <spdlog/spdlog.h>
#include <sys/prctl.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <signal.h>
#include <unistd.h>

#include "serverStarterHelper.hpp"

[[nodiscard]] ServerHandle startServer()
{
	constexpr std::string_view executable_name { "IDHANServer" };
	const std::filesystem::path current_dir { std::filesystem::current_path() };
	const auto executable { current_dir / executable_name };
	if ( !std::filesystem::exists( executable ) ) throw std::runtime_error( "IDHANServer executable does not exist" );

	const std::array< char*, 3 > args { const_cast< char* >( executable.c_str() ), "--testmode=1", nullptr };

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
