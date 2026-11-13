#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "scripts/BytecodeCache.hpp"
#include "scripts/ScriptRegistry.hpp"

using namespace idhan::downloader;

TEST_CASE( "The shipped URL classes route their own example URLs", "[downloader][examples]" )
{
	const std::filesystem::path parsers { IDHAN_DOWNLOADER_PARSER_DIR };
	auto registry { ScriptRegistry::create( {}, parsers ) };
	INFO( ( registry.has_value() ? "" : registry.error() ) );
	REQUIRE( registry.has_value() );

	const std::vector< std::pair< std::string, std::string > > expected {
		{ "https://e621.net/posts", "search" },
		{ "https://mega.nz/file/example#key", "shared" },
		{ "https://mega.nz/folder/example#key/file/child", "shared" },
		{ "https://mega.co.nz/#!example!key", "shared" },
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
