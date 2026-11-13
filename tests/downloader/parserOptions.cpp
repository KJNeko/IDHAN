#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>

#include "harness/Fixtures.hpp"
#include "js/JSValueHandle.hpp"
#include "js/ScriptContext.hpp"
#include "js/ScriptExecution.hpp"
#include "js/bindings/IdhanBindings.hpp"
#include "scripts/ParserSDKVersion.hpp"

namespace idhan::downloader
{
std::expected< Json::Value, std::string > loadParserMetadata(
	BytecodeCache& bytecode,
	const std::filesystem::path& path );
}

namespace idhan::test
{

TEST_CASE( "Parser SDK versions enforce the supported release", "[downloader][sdk]" )
{
	CHECK( downloader::compatibleParserSDKVersion( "1.0.0" ) );
	CHECK( downloader::compatibleParserSDKVersion( "1.1.0" ) );
	CHECK_FALSE( downloader::compatibleParserSDKVersion( "2.0.0" ) );
	CHECK_FALSE( downloader::compatibleParserSDKVersion( "0.9.9" ) );
	CHECK_FALSE( downloader::compatibleParserSDKVersion( "1.0" ) );
	CHECK_FALSE( downloader::compatibleParserSDKVersion( "1.0.0-beta" ) );
	CHECK_FALSE( downloader::compatibleParserSDKVersion( "01.0.0" ) );
}

TEST_CASE( "Option metadata comes from declarations without constructing parsers", "[downloader][options]" )
{
	const std::filesystem::path root { IDHAN_DOWNLOADER_TEST_PARSER_DIR };
	downloader::BytecodeCache cache { root };
	const auto metadata_e { downloader::loadParserMetadata( cache, root / "options.js" ) };
	INFO( ( metadata_e.has_value() ? "" : metadata_e.error() ) );
	REQUIRE( metadata_e.has_value() );
	const auto& options { metadata_e.value()[ "options" ] };
	REQUIRE( options.size() == 5 );
	CHECK( options[ 0 ][ "type" ].asString() == "boolean" );
	CHECK_FALSE( options[ 0 ][ "default" ].asBool() );
	CHECK( options[ 1 ][ "type" ].asString() == "number" );
	CHECK( options[ 1 ][ "default" ].asInt() == 2 );
	CHECK( options[ 1 ][ "minimum" ].asInt() == 0 );
	CHECK( options[ 1 ][ "integer" ].asBool() );
	CHECK( options[ 2 ][ "type" ].asString() == "string" );
	CHECK( options[ 2 ][ "default" ].asString() == "example" );
	CHECK( options[ 3 ][ "type" ].asString() == "string[]" );
	CHECK( options[ 3 ][ "default" ].isArray() );
	CHECK( options[ 3 ][ "default" ].empty() );
	CHECK( options[ 4 ][ "type" ].asString() == "string[]" );
	CHECK( options[ 4 ][ "default" ][ 0 ].asString() == "one" );

	downloader::ScriptContext scripts { {}, cache };
	downloader::ScriptExecution execution;
	const auto realm_e { scripts.createRealm( execution ) };
	REQUIRE( realm_e.has_value() );
	downloader::JSContextPtr realm { realm_e.value() };
	execution.context = realm.get();
	const auto evaluated_e { scripts.evaluate( execution, root / "options.js" ) };
	REQUIRE( evaluated_e.has_value() );
	downloader::JSValueHandle evaluated { realm.get(), evaluated_e.value() };
	std::string error;
	REQUIRE( scripts.runReadyMicrotasks( error ) );
	downloader::JSValueHandle module { realm.get(), JS_GetModuleNamespace( realm.get(), execution.module ) };
	downloader::JSValueHandle constructions {
		realm.get(), JS_GetPropertyStr( realm.get(), module.get(), "constructions" )
	};
	int count {};
	REQUIRE( JS_ToInt32( realm.get(), &count, constructions.get() ) == 0 );
	CHECK( count == 0 );
	downloader::JSValueHandle verify { realm.get(), JS_GetPropertyStr( realm.get(), module.get(), "verify" ) };
	downloader::JSValueHandle verified { realm.get(), JS_Call( realm.get(), verify.get(), JS_UNDEFINED, 0, nullptr ) };
	const bool failed { JS_IsException( verified.get() ) != 0 };
	INFO( ( failed ? downloader::scriptExceptionString( realm.get() ) : "" ) );
	REQUIRE_FALSE( failed );
	const auto result_e { downloader::scriptValueToJson( realm.get(), verified.get() ) };
	REQUIRE( result_e.has_value() );
	const auto& result { result_e.value() };
	CHECK( result[ "enabled" ].asBool() );
	CHECK( result[ "count" ].asInt() == 3 );
	CHECK( result[ "tags" ][ 0 ].asString() == "cat" );
	CHECK_FALSE( result[ "defaults" ][ "enabled" ].asBool() );
	CHECK( result[ "defaults" ][ "count" ].asInt() == 2 );
	CHECK( result[ "defaults" ][ "tags" ].empty() );
	CHECK( result[ "independent" ].asBool() );
	CHECK( result[ "frozen" ].asBool() );
	CHECK( result[ "rejected" ].size() == 5 );
}

} // namespace idhan::test
