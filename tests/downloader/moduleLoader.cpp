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

	for ( const std::string_view name : { "zod.js", "validation.js", "parser.js" } )
	{
		const auto source {
			name == "zod.js" ? std::filesystem::path { IDHAN_DOWNLOADER_PARSER_DIR } / "packages" / name :
							   std::filesystem::path { IDHAN_SOURCE_DIR } / "IDHANDownloader/premade/packages" / name
		};
		std::filesystem::copy_file( source, packages / name );
	}
}

TEST_CASE( "Shipped parsers preserve their import and URL contracts", "[downloader][parser-contracts]" )
{
	std::string entry {};
	SECTION( "e621 posts, siblings and galleries" )
	{
		entry = "e621Contract";
	}
	SECTION( "Gelbooru tag categories and credentials" )
	{
		entry = "gelbooruContract";
	}
	SECTION( "Pixiv pages and artist galleries" )
	{
		entry = "pixivContract";
	}
	SECTION( "HyShare hostnames, redirects and identity" )
	{
		entry = "hyshareContract";
	}
	SECTION( "pawchive attachments and creator pagination" )
	{
		entry = "pawchiveContract";
	}
	SECTION( "Shared import and response helpers" )
	{
		entry = "helpersContract";
	}

	Harness harness {};
	copyPackages( harness );
	const std::filesystem::path source { IDHAN_SOURCE_DIR };
	for ( const std::string_view name : { "e621.js", "gelbooru.js", "hyshare.js", "pawchive.js", "pixiv.js" } )
	{
		std::filesystem::copy_file( source / "IDHANDownloader/premade" / name, harness.parsers.root() / name );
	}
	std::filesystem::copy_file(
		source / "tests/downloader/testdata/parser-contracts.js", harness.parsers.root() / "test.js" );
	harness.parsers.write( "packages/pawchive-api.js", R"JS(
export class ApiClient {
    constructor() { this.defaultHeaders = {}; }
}
function response(args) {
    globalThis.pawchiveCalls.push(args);
    if (globalThis.pawchiveResponses.length === 0) throw new Error("Unexpected pawchive API call");
    return Promise.resolve(globalThis.pawchiveResponses.shift());
}
export class PostsApi {
    serviceUserCreatorIdGet(...args) { return response(args); }
    serviceUserCreatorIdPostPostIdGet(...args) { return response(args); }
}
)JS" );
	harness.configure( std::format( R"([{{"export":"{}","path":"/page"}}])", entry ) );
	const auto session { harness.session( harness.server.url( "/page" ) ) };
	REQUIRE( session->submit( harness.server.url( "/page" ) ).has_value() );
	session->wait();
	idhan::test::checkNoFailures( harness.observer );
	CHECK( harness.observer.completed.size() == 1 );
}

TEST_CASE( "HyShare assigns tags to the full source hostname", "[downloader][modules][hyshare]" )
{
	Harness harness {};
	copyPackages( harness );
	std::filesystem::copy_file(
		std::filesystem::path { IDHAN_DOWNLOADER_PARSER_DIR } / "hyshare.js", harness.parsers.root() / "hyshare.js" );
	harness.parsers.write( "test.js", R"JS(
import {post} from "./hyshare.js";

const hash = "a".repeat(64);

function mockHost(expectedHostname) {
    return {
        imports: 0,
        async request(request) {
            return {status: 200, url: request.url, body: JSON.stringify({
                hash, ext: ".png",
                tag_services_to_tags: {one: ["landscape"], two: ["landscape", "favorite"]},
            })};
        },
        import(options) {
            const domains = Object.keys(options.tags);
            if (domains.length !== 1 || domains[0] !== expectedHostname) {
                throw new Error(`Wrong tag domains: ${JSON.stringify(domains)}`);
            }
            if (options.tags[expectedHostname].slice().sort().join(",") !== "favorite,landscape") {
                throw new Error("HyShare tags were lost or duplicated");
            }
            this.imports += 1;
        },
    };
}

export async function page() {
    const cases = [
        ["https://testing.mydomain.com:8443", "testing.mydomain.com"],
        ["https://other.mydomain.com", "other.mydomain.com"],
        ["https://user:password@testing.example.co.uk:8443", "testing.example.co.uk"],
        ["http://127.0.0.1:9000", "127.0.0.1"],
    ];
    for (const [base, hostname] of cases) {
        const host = mockHost(hostname);
        await post({url: `${base}/view/${hash}?source=example#fragment`}, host);
        if (host.imports !== 1) throw new Error("HyShare did not import the file");
    }
}
)JS" );
	harness.configure( R"([{"export":"page","path":"/page"}])" );
	const auto session { harness.session( harness.server.url( "/page" ) ) };
	REQUIRE( session->submit( harness.server.url( "/page" ) ).has_value() );
	session->wait();
	idhan::test::checkNoFailures( harness.observer );
	CHECK( harness.observer.completed.size() == 1 );
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
