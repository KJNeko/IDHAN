#include <catch2/catch_test_macros.hpp>

#include "harness/Fixtures.hpp"
#include "scripts/BytecodeCache.hpp"

namespace idhan::test
{
TEST_CASE( "Runtime modules use JavaScript without TypeScript fallback", "[downloader][modules]" )
{
	ParserFixture files;
	files.write( "main.js", "import { value } from './dependency.js'; export const result = value;" );
	files.write( "dependency.js", "export const value = 42;" );
	files.write( "dependency.ts", "This must never be read" );
	downloader::BytecodeCache cache { files.root() };
	const auto compiled_e { cache.bytecode( files.root() / "main.js" ) };
	REQUIRE( compiled_e.has_value() );

	const auto typescript_e { cache.bytecode( files.root() / "dependency.ts" ) };
	REQUIRE_FALSE( typescript_e.has_value() );
	CHECK( typescript_e.error().contains( "compile TypeScript externally" ) );

	files.write( "only.ts", "export const value = 1;" );
	const auto missing_e { cache.resolve( "./only.js", files.root() / "main.js" ) };
	CHECK_FALSE( missing_e.has_value() );
}
}
