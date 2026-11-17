
#include <spdlog/spdlog.h>

#include <cstdlib>

#include "idhan/downloader/ParserRegistry.hpp"

int main( int argc, char** argv )
{
	spdlog::set_level( spdlog::level::trace );

	ParserRegistry manifest { "./downloaders/parsers" };

	return EXIT_SUCCESS;
}
