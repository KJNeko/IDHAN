#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "harness/Fixtures.hpp"
#include "js/ScriptContext.hpp"
#include "js/ScriptExecution.hpp"
#include "js/JSValueHandle.hpp"
#include "scripts/ScriptRegistry.hpp"
#include "scripts/ScriptWatcher.hpp"
#include "harness/Harness.hpp"

namespace idhan::test
{

std::string updateParserSource( const std::string& id, const int version )
{
	return "export const revision = " + std::to_string( version )
	     + ";\n"
	       "export const run = () => revision;\n"
	       "export default class Parser { static metadata = { sdkVersion: '1.0.0', id: '"
	     + id
	     + "', version: revision, "
	       "options: [], handlers: [], discoveries: [], urlClasses: [{ id: '" + id + "', parser: '" + id + "', "
		"hosts: ['" + id + ".test'], url_patterns: [{ generator: 'post', patterns: [{ pathname: '/posts/:id' }] }], "
		"generators: [{ id: 'post', handler: 'post' }] }] }; }";
}

downloader::ScriptRoute updateTestRoute( downloader::ScriptRegistry& registry, const std::string& id )
{
	const auto route_e { registry.route( "https://" + id + ".test/posts/1" ) };
	REQUIRE( route_e.has_value() );
	REQUIRE( route_e.value().has_value() );
	const auto& route { route_e.value().value() };
	REQUIRE_FALSE( route.download );
	REQUIRE( route.bytecode != nullptr );
	return route;
}

int updateTestRevision( downloader::ScriptRegistry& registry, downloader::ScriptRoute route )
{
	downloader::ScriptContext scripts { {}, registry.bytecode() };
	downloader::ScriptExecution execution;
	execution.route = std::move( route );
	const auto realm_e { scripts.createRealm( execution ) };
	REQUIRE( realm_e.has_value() );
	downloader::JSContextPtr realm { realm_e.value() };
	execution.context = realm.get();
	const auto evaluated_e { scripts.evaluate( execution, execution.route.script ) };
	REQUIRE( evaluated_e.has_value() );
	downloader::JSValueHandle evaluated { realm.get(), evaluated_e.value() };
	std::string error;
	REQUIRE( scripts.runReadyMicrotasks( error ) );
	downloader::JSValueHandle module { realm.get(), JS_GetModuleNamespace( realm.get(), execution.module ) };
	downloader::JSValueHandle revision { realm.get(), JS_GetPropertyStr( realm.get(), module.get(), "revision" ) };
	int value {};
	REQUIRE( JS_ToInt32( realm.get(), &value, revision.get() ) == 0 );
	return value;
}

TEST_CASE( "Parser uploads require replacement consent and roll back invalid sources", "[downloader][parser-upload]" )
{
	ParserFixture files;
	std::filesystem::remove( files.root() / "test.js" );
	auto registry_e { downloader::ScriptRegistry::create( {}, files.root() ) };
	REQUIRE( registry_e.has_value() );
	auto& registry { *registry_e.value() };
	const auto source { updateParserSource( "uploaded", 1 ) };
	REQUIRE( registry.installParser( "uploaded.js", source, false ).has_value() );
	CHECK_FALSE( registry.installParser( "uploaded.js", updateParserSource( "uploaded", 2 ), false ).has_value() );
	CHECK_FALSE( registry.installParser( "uploaded.js", "invalid syntax !", true ).has_value() );
	const auto exported_e { registry.parserSource( "uploaded.js" ) };
	REQUIRE( exported_e.has_value() );
	CHECK( exported_e.value() == source );
	CHECK( updateTestRevision( registry, updateTestRoute( registry, "uploaded" ) ) == 1 );
	REQUIRE( registry.installParser( "uploaded.js", updateParserSource( "uploaded", 2 ), true ).has_value() );
	CHECK( updateTestRevision( registry, updateTestRoute( registry, "uploaded" ) ) == 2 );
	CHECK_FALSE( registry.installParser( "../escape.js", source, false ).has_value() );
	CHECK_FALSE( registry.parserSource( "../escape.js" ).has_value() );
}

TEST_CASE( "Parser preparation isolates invalid files and supports selective replacement", "[downloader][parser-updates]" )
{
	ParserFixture files;
	std::filesystem::remove( files.root() / "test.js" );
	files.write( "alpha.js", updateParserSource( "alpha", 1 ) );
	files.write( "broken.js", "export const invalid = ;" );
	auto registry_e { downloader::ScriptRegistry::create( files.urlClassesPath(), files.root() ) };
	INFO( ( registry_e.has_value() ? "" : registry_e.error() ) );
	REQUIRE( registry_e.has_value() );
	auto& registry { *registry_e.value() };
	const auto initial { registry.parserStatus() };
	REQUIRE( initial.size() == 2 );
	CHECK( initial[ 0 ].active );
	CHECK_FALSE( initial[ 1 ].active );
	REQUIRE( initial[ 1 ].error.has_value() );
	CHECK( initial[ 1 ].error->contains( "SyntaxError" ) );
	const auto old_route { updateTestRoute( registry, "alpha" ) };
	CHECK( updateTestRevision( registry, old_route ) == 1 );

	files.write( "alpha.js", updateParserSource( "alpha", 2 ) );
	const auto updated_e { registry.updateParser( "alpha.js" ) };
	REQUIRE( updated_e.has_value() );
	REQUIRE( updated_e.value().size() == 1 );
	CHECK_FALSE( updated_e.value()[ 0 ].error.has_value() );
	CHECK( updateTestRevision( registry, updateTestRoute( registry, "alpha" ) ) == 2 );
	CHECK( updateTestRevision( registry, old_route ) == 1 );

	// A broken edit cannot replace the working parser.
	files.write( "alpha.js", "export const invalid = ;" );
	const auto failed_e { registry.updateParser( files.root() / "alpha.js" ) };
	REQUIRE( failed_e.has_value() );
	CHECK( failed_e.value()[ 0 ].active );
	CHECK( failed_e.value()[ 0 ].error.has_value() );
	CHECK( updateTestRevision( registry, updateTestRoute( registry, "alpha" ) ) == 2 );

	// Installing beta succeeds while alpha's source remains invalid, without reloading alpha.
	files.write( "beta.js", updateParserSource( "beta", 1 ) );
	const auto installed_e { registry.updateParser( "beta.js" ) };
	REQUIRE( installed_e.has_value() );
	CHECK_FALSE( installed_e.value()[ 0 ].error.has_value() );
	CHECK( updateTestRevision( registry, updateTestRoute( registry, "alpha" ) ) == 2 );
	CHECK( updateTestRevision( registry, updateTestRoute( registry, "beta" ) ) == 1 );

	std::filesystem::remove( files.root() / "alpha.js" );
	const auto refreshed_e { registry.updateParsers() };
	REQUIRE( refreshed_e.has_value() );
	const auto removed_e { registry.route( "https://alpha.test/posts/1" ) };
	REQUIRE( removed_e.has_value() );
	REQUIRE( removed_e.value().has_value() );
	CHECK( removed_e.value()->download );
	CHECK( updateTestRevision( registry, old_route ) == 1 );

	ParserFixture outside;
	outside.write( "outside.js", updateParserSource( "outside", 1 ) );
	const auto outside_e { registry.updateParser( outside.root() / "outside.js" ) };
	REQUIRE_FALSE( outside_e.has_value() );
	CHECK( outside_e.error().contains( "outside" ) );
}

TEST_CASE( "Selective parser updates detect incompatible shared dependencies", "[downloader][parser-updates]" )
{
	ParserFixture files;
	std::filesystem::create_directories( files.root() / "packages" );
	files.write( "packages/shared.js", "export const shared = 1;" );
	const std::string dependency { "import { shared } from './packages/shared.js'; export const dependency = shared;\n" };
	files.write( "alpha.js", dependency + updateParserSource( "alpha", 1 ) );
	files.write( "beta.js", dependency + updateParserSource( "beta", 1 ) );
	auto registry_e { downloader::ScriptRegistry::create( files.urlClassesPath(), files.root() ) };
	REQUIRE( registry_e.has_value() );
	auto& registry { *registry_e.value() };
	const auto old { updateTestRoute( registry, "alpha" ) };
	files.write( "packages/shared.js", "export const shared = 2;" );
	const auto conflict_e { registry.updateParser( "alpha.js" ) };
	REQUIRE_FALSE( conflict_e.has_value() );
	CHECK( conflict_e.error().contains( "Shared parser dependency changed" ) );
	CHECK( updateTestRoute( registry, "alpha" ).bytecode == old.bytecode );
	const auto refreshed_e { registry.updateParsers() };
	INFO( ( refreshed_e.has_value() ? "" : refreshed_e.error() ) );
	REQUIRE( refreshed_e.has_value() );
	CHECK( updateTestRoute( registry, "alpha" ).bytecode != old.bytecode );
}

TEST_CASE( "Bundled parser metadata is prepared at startup", "[downloader][parser-updates]" )
{
	const auto root { std::filesystem::path { IDHAN_DOWNLOADER_PARSER_DIR } };
	auto registry_e { downloader::ScriptRegistry::create( root / "no-external-classes.json", root ) };
	INFO( ( registry_e.has_value() ? "" : registry_e.error() ) );
	REQUIRE( registry_e.has_value() );
	const auto report { registry_e.value()->parserStatus() };
	REQUIRE( report.size() == 6 );

	for ( const auto& parser : report )
	{
		INFO( parser.path.string() );
		INFO( parser.error.value_or( "" ) );
		CHECK( parser.active );
		CHECK_FALSE( parser.error.has_value() );
	}
}

TEST_CASE(
	"JavaScript watching reloads edits, additions and deletions while retaining old routes",
	"[downloader][script-watcher]" )
{
	ParserFixture files;
	std::filesystem::remove( files.root() / "test.js" );
	files.write( "alpha.js", updateParserSource( "alpha", 1 ) );
	auto registry_e { downloader::ScriptRegistry::create( {}, files.root() ) };
	REQUIRE( registry_e.has_value() );
	auto& registry { *registry_e.value() };
	const auto old_route { updateTestRoute( registry, "alpha" ) };
	downloader::ScriptWatcher watcher {
		registry, files.root(), {}, std::chrono::milliseconds { 20 }, std::chrono::milliseconds { 40 }
	};

	files.write( "alpha.js.tmp", updateParserSource( "alpha", 2 ) );
	std::filesystem::rename( files.root() / "alpha.js.tmp", files.root() / "alpha.js" );
	REQUIRE( waitUntil( [ &registry ] { return registry.parsers()[ 0 ][ "metadata" ][ "version" ].asInt() == 2; } ) );
	CHECK( updateTestRevision( registry, updateTestRoute( registry, "alpha" ) ) == 2 );
	CHECK( updateTestRevision( registry, old_route ) == 1 );

	files.write( "alpha.js", "export const broken = ;" );
	REQUIRE( waitUntil( [ &registry ] { return registry.parserStatus()[ 0 ].error.has_value(); } ) );
	CHECK( updateTestRevision( registry, updateTestRoute( registry, "alpha" ) ) == 2 );

	files.write( "alpha.js", updateParserSource( "alpha", 3 ) );
	REQUIRE( waitUntil( [ &registry ] { return registry.parsers()[ 0 ][ "metadata" ][ "version" ].asInt() == 3; } ) );
	files.write( "beta.mjs", updateParserSource( "beta", 1 ) );
	REQUIRE( waitUntil( [ &registry ] { return registry.parsers().size() == 2; } ) );
	std::filesystem::remove( files.root() / "alpha.js" );
	REQUIRE( waitUntil( [ &registry ] { return registry.parsers().size() == 1; } ) );
	CHECK( updateTestRevision( registry, old_route ) == 1 );
}

TEST_CASE( "JavaScript watching includes shared modules", "[downloader][script-watcher]" )
{
	ParserFixture files;
	std::filesystem::remove( files.root() / "test.js" );
	std::filesystem::create_directories( files.root() / "packages" );
	files.write( "packages/shared.js", "export const shared = 1;" );
	auto source { updateParserSource( "alpha", 1 ) };
	source.replace( source.find( "revision = 1" ), std::string { "revision = 1" }.size(), "revision = shared" );
	files.write( "alpha.js", "import { shared } from './packages/shared.js';\n" + source );
	auto registry_e { downloader::ScriptRegistry::create( {}, files.root() ) };
	REQUIRE( registry_e.has_value() );
	auto& registry { *registry_e.value() };
	downloader::ScriptWatcher watcher {
		registry, files.root(), {}, std::chrono::milliseconds { 20 }, std::chrono::milliseconds { 40 }
	};
	files.write( "packages/shared.js", "export const shared = 2;" );
	REQUIRE( waitUntil( [ &registry ] { return registry.parsers()[ 0 ][ "metadata" ][ "version" ].asInt() == 2; } ) );
}

TEST_CASE( "JavaScript watching reloads configured URL class files", "[downloader][script-watcher]" )
{
	ParserFixture files;
	std::filesystem::remove( files.root() / "test.js" );
	files.write( "alpha.js", updateParserSource( "alpha", 1 ) );
	files.write( "url-classes.json", R"({"urlClasses":[]})" );
	auto registry_e { downloader::ScriptRegistry::create( files.urlClassesPath(), files.root() ) };
	REQUIRE( registry_e.has_value() );
	auto& registry { *registry_e.value() };
	downloader::ScriptWatcher watcher {
		registry,
		files.root(),
		files.urlClassesPath(),
		std::chrono::milliseconds { 20 },
		std::chrono::milliseconds { 40 }
	};
	files.write( "url-classes.json", R"({"urlClasses":[{
        "name":"extra", "hosts":["extra.test"], "routes":[],
        "requestRate":{"requests":2,"seconds":1}
    }]})" );
	REQUIRE( waitUntil(
		[ &registry ]
		{
			const auto rate { registry.laneSettings( "extra.test" ).rate };
			return rate.has_value() && rate.value().requests == 2;
		} ) );
}

} // namespace idhan::test
