
#include <spdlog/spdlog.h>

#include <cstdlib>

#include "idhan/downloader/ParserRegistry.hpp"

int main( int argc, char** argv )
{
	spdlog::set_level( spdlog::level::trace );

	ParserRegistry manifest { "./downloaders/parsers" };

	const std::vector< std::string > test_urls {
		"https://gelbooru.com/index.php?page=post&s=view&id=15276023&tags=loli+cat_ears",
		"https://gelbooru.com/index.php?page=post&s=view&id=15276439"
	};

	for ( const auto& test_url : test_urls )
	{
		const auto parser_info { manifest.tryParseUrl( test_url ) };
	}

	return EXIT_SUCCESS;
}
