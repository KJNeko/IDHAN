//
// Created by kj16609 on 7/23/24.
//

#include <cstdlib>

#include "ConnectionArguments.hpp"
#include "ServerContext.hpp"
#include "logging/log.hpp"

int main( int argc, char** argv )
{
	spdlog::set_level( spdlog::level::debug );

	idhan::ConnectionArguments arguments {};
	//arguments.hydrus_info.hydrus_db_path = "/home/kj16609/.local/share/hydrus/db/";
	arguments.user = "idhan";
	arguments.hostname = "localhost";

	for ( std::size_t i = 0; i < argc; ++i )
	{
		idhan::log::debug( "{}: {}", i, argv[ i ] );
		if ( argv[ i ] == "--testmode" ) arguments.testmode = true;
	}

	idhan::ServerContext context { arguments };

	// context.cloneHydrusData( "/home/kj16609/.local/share/hydrus/db" );
	// context.cloneHydrusData( "/home/kj16609/Desktop/Projects/cxx/IDHAN/3rd-party/hydrus/db" );

	context.run();

	idhan::log::info( "Shutting down..." );

	return EXIT_SUCCESS;
}
