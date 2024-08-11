//
// Created by kj16609 on 7/23/24.
//

#include <cstdlib>

#include "ConnectionArguments.hpp"
#include "ServerContext.hpp"

int main( int argc, char** argv )
{
	idhan::ConnectionArguments arguments {};
	//arguments.hydrus_info.hydrus_db_path = "/home/kj16609/.local/share/hydrus/db/";
	arguments.user = "idhan";
	arguments.hostname = "localhost";

	idhan::ServerContext context { arguments };

	context.run();

	return EXIT_SUCCESS;
}
