#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "Harness.hpp"

using namespace idhan;
using namespace idhan::downloader;
using idhan::test::Harness;
using idhan::test::waitUntil;

static void copyPackages( Harness& harness )
{
	const auto packages { harness.parsers.root() / "packages" };
	std::filesystem::create_directories( packages );

	for ( const std::string_view name : { "zod.js", "validation.js" } )
	{
		std::filesystem::copy_file(
			std::filesystem::path { IDHAN_DOWNLOADER_PARSER_DIR } / "packages" / name, packages / name );
	}
}

TEST_CASE( "A parser can import a helper module beside it", "[downloader][modules]" )
{
	Harness harness {};
	harness.server.route( "/page", []( const test::TestRequest& ) { return test::TestResponse { .body = "ok" }; } );

	harness.parsers.write( "helpers.js", R"JS(
export function tag(value) {
    return `tagged:${value}`;
}
)JS" );
	harness.parsers.write( "test.js", R"JS(
import {tag} from "./helpers.js";

export async function page(input, idhan) {
    const response = await idhan.request({url: input.url});
    if (tag(response.body) !== "tagged:ok") throw new Error("the imported helper did not run");
}
)JS" );
	harness.configure( R"([{"export":"page","path":"/page"}])" );

	const auto session { harness.session( harness.server.url( "/page" ) ) };
	REQUIRE( session->submit( harness.server.url( "/page" ) ).has_value() );
	REQUIRE( waitUntil( [ & ] { return session->idle(); } ) );

	idhan::test::checkNoFailures( harness.observer );
}

TEST_CASE( "A parser can validate response JSON with bundled Zod", "[downloader][modules]" )
{
	Harness harness {};
	harness.server.route(
		"/valid",
		[]( const test::TestRequest& )
		{ return test::TestResponse { .body = R"({"name":"example","values":[1,2,3]})" }; } );
	harness.server.route(
		"/invalid",
		[]( const test::TestRequest& )
		{ return test::TestResponse { .body = R"({"name":"example","values":[1,"two"]})" }; } );

	copyPackages( harness );
	harness.parsers.write( "test.js", R"JS(
import {parseResponse, z} from "validation";

const responseSchema = z.object({
    name: z.string(),
    values: z.array(z.number()),
});

export async function page(input, idhan) {
    const response = await idhan.request({url: input.url, responseType: "json"});
    parseResponse(responseSchema, response.body, "fixture API");
}
)JS" );
	harness.configure( R"([{"export":"page","path":"/valid"},{"export":"page","path":"/invalid"}])" );

	const auto session { harness.session( harness.server.url( "/valid" ) ) };
	REQUIRE( session->submit( harness.server.url( "/valid" ) ).has_value() );
	REQUIRE( waitUntil( [ & ] { return session->idle(); } ) );
	idhan::test::checkNoFailures( harness.observer );

	REQUIRE( session->submit( harness.server.url( "/invalid" ) ).has_value() );
	REQUIRE( waitUntil( [ & ] { return session->idle(); } ) );
	REQUIRE( harness.observer.failures.size() == 1 );
	CHECK( harness.observer.failures.front().second.find( "fixture API" ) != std::string::npos );
	CHECK( harness.observer.failures.front().second.find( "values" ) != std::string::npos );
}

TEST_CASE( "A parser reports the status before it parses a body", "[downloader][modules]" )
{
	Harness harness {};
	harness.server.route(
		"/denied",
		[]( const test::TestRequest& )
		{
			return test::TestResponse {
				.status = 403,
				.reason = "Forbidden",
				.headers = { { "Content-Type", "text/html; charset=utf-8" } },
				.body = "<html>\n  <body>Access denied</body>\n</html>"
			};
		} );

	copyPackages( harness );
	harness.parsers.write( "test.js", R"JS(
import {parseJsonResponse, z} from "validation";

const responseSchema = z.object({name: z.string()});

export async function page(input, idhan) {
    const response = await idhan.request({url: input.url, responseType: "text"});
    parseJsonResponse(responseSchema, response, "fixture API");
}
)JS" );
	harness.configure( R"([{"export":"page","path":"/denied"}])" );

	const auto session { harness.session( harness.server.url( "/denied" ) ) };
	REQUIRE( session->submit( harness.server.url( "/denied" ) ).has_value() );
	REQUIRE( waitUntil( [ & ] { return session->idle(); } ) );

	REQUIRE( harness.observer.failures.size() == 1 );

	const std::string& error { harness.observer.failures.front().second };
	CHECK( error.contains( "fixture API returned HTTP 403" ) );
	CHECK( error.contains( "text/html" ) );
	CHECK( error.contains( "<html> <body>Access denied</body> </html>" ) );
}

TEST_CASE( "A module escaping the parser directory is refused", "[downloader][modules]" )
{
	Harness harness {};
	harness.server.route( "/page", []( const test::TestRequest& ) { return test::TestResponse { .body = "ok" }; } );

	harness.parsers.write( "test.js", R"JS(
import {secret} from "../../../etc/passwd";

export async function page() {}
)JS" );
	harness.configure( R"([{"export":"page","path":"/page"}])" );

	const auto session { harness.session( harness.server.url( "/page" ) ) };
	REQUIRE( session->submit( harness.server.url( "/page" ) ).has_value() );
	REQUIRE( waitUntil( [ & ] { return session->idle(); } ) );

	REQUIRE( harness.observer.failures.size() == 1 );
	CHECK( harness.server.requestCount() == 0 );
}

TEST_CASE( "A module symlink escaping the parser directory is refused", "[downloader][modules]" )
{
	Harness harness {};
	const auto outside { harness.parsers.root().parent_path() / "idhan-downloader-outside-module.js" };
	std::ofstream { outside } << "export const secret = 'escaped';\n";
	const auto link { harness.parsers.root() / "linked.js" };
	std::filesystem::create_symlink( outside, link );

	struct Cleanup
	{
		std::filesystem::path path;

		~Cleanup()
		{
			std::error_code error {};
			std::filesystem::remove( path, error );
		}
	} cleanup { outside };

	harness.parsers.write( "test.js", R"JS(
import {secret} from "./linked.js";

export async function page() {
    if (secret !== "escaped") throw new Error("unexpected module");
}
)JS" );
	harness.configure( R"([{"export":"page","path":"/page"}])" );

	const auto session { harness.session( harness.server.url( "/page" ) ) };
	REQUIRE( session->submit( harness.server.url( "/page" ) ).has_value() );
	REQUIRE( waitUntil( [ & ] { return session->idle(); } ) );

	REQUIRE( harness.observer.failures.size() == 1 );
}

TEST_CASE( "A script compiled once serves every work item", "[downloader][modules]" )
{
	Harness harness {};
	harness.server.route( "/page", []( const test::TestRequest& ) { return test::TestResponse { .body = "ok" }; } );

	harness.parsers.write( "test.js", R"JS(
export async function page(input, idhan) {
    await idhan.request({url: input.url});
}
)JS" );
	harness.configure( R"([{"export":"page","path":"/page"}])" );

	const auto session { harness.session( harness.server.url( "/page" ) ) };

	for ( int index = 0; index < 5; ++index ) REQUIRE( session->submit( harness.server.url( "/page" ) ).has_value() );

	REQUIRE( waitUntil( [ & ] { return session->idle(); } ) );

	idhan::test::checkNoFailures( harness.observer );
	CHECK( harness.observer.started.size() == 5 );

	std::filesystem::remove( harness.parsers.root() / "test.js" );

	REQUIRE( session->submit( harness.server.url( "/page" ) ).has_value() );
	REQUIRE( waitUntil( [ & ] { return session->idle(); } ) );

	idhan::test::checkNoFailures( harness.observer );
	CHECK( harness.observer.started.size() == 6 );
}
