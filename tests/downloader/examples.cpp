#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "scripts/BytecodeCache.hpp"
#include "scripts/ScriptRegistry.hpp"

using namespace idhan::downloader;

TEST_CASE( "Every shipped parser compiles", "[downloader][examples]" )
{
	const std::filesystem::path parsers { IDHAN_DOWNLOADER_PARSER_DIR };
	auto registry { ScriptRegistry::create( parsers / "url-classes.json", parsers ) };
	REQUIRE( registry.has_value() );

	for ( const auto& entry : std::filesystem::directory_iterator { parsers } )
	{
		if ( entry.path().extension() != ".js" ) continue;

		const auto compiled { ( *registry )->bytecode().bytecode( entry.path() ) };
		INFO( entry.path().filename().string() << ": " << ( compiled ? "" : compiled.error() ) );
		REQUIRE( compiled.has_value() );
		CHECK( !compiled->empty() );
	}
}

TEST_CASE( "The shipped URL classes route their own example URLs", "[downloader][examples]" )
{
	const std::filesystem::path parsers { IDHAN_DOWNLOADER_PARSER_DIR };
	auto registry { ScriptRegistry::create( parsers / "url-classes.json", parsers ) };
	REQUIRE( registry.has_value() );

	const std::vector< std::pair< std::string, std::string > > expected {
		{ "https://e621.net/posts", "gallery" },
		{ "https://e621.net/posts/12345", "post" },
		{ "https://www.pixiv.net/en/artworks/1", "post" },
		{ "https://www.pixiv.net/users/1", "user" }
	};

	for ( const auto& [ url, export_name ] : expected )
	{
		const auto route { ( *registry )->route( url ) };
		INFO( url );
		REQUIRE( route.has_value() );
		REQUIRE( route->has_value() );
		CHECK( ( *route )->export_name == export_name );
	}
}
