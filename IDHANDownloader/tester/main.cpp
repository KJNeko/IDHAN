
#include <spdlog/spdlog.h>

#include <cstdlib>

#include "idhan/downloader/ParserRegistry.hpp"

int main( int argc, char** argv )
{
	spdlog::set_level( spdlog::level::trace );

	ParserRegistry manifest { "./downloaders/parsers" };

	const std::vector< std::string > test_urls {
		"https://gelbooru.com/index.php?page=post&s=view&id=15276023&tags=loli+cat_ears",
		"https://gelbooru.com/index.php?page=post&s=view&id=15276439",
		"https://gelbooru.com/index.php?page=post&s=list&tags=loli+pussy",
		"https://gelbooru.com/index.php?page=post&s=list",
		"https://gelbooru.com/index.php?page=post",
		"https://mega.nz/folder/fkBzwQiC#OasqyUGzA3kGWs6TFAQt_Q"
	};

	for ( const auto& test_url : test_urls )
	{
		const auto parser_info { manifest.tryParseUrl( test_url ) };
		if ( !parser_info.has_value() )
		{
			spdlog::info( "No parser matched {}", test_url );
			continue;
		}

		const auto& [ url_class, pattern, generator, captures ] { parser_info.value().match };
		spdlog::info(
			"Matched {}: class='{}', pattern='{}', generator='{}'",
			test_url,
			url_class,
			pattern,
			generator.value_or( "none" ) );

		for ( const auto& [ name, value ] : captures )
		{
			spdlog::info( "  Capture {} = '{}'", name, value );
		}
	}

	return EXIT_SUCCESS;
}
