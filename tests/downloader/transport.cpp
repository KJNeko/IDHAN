#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

#include "Harness.hpp"

using namespace idhan;
using namespace idhan::downloader;
using idhan::test::Harness;
using idhan::test::waitUntil;

namespace
{
constexpr std::string_view passthrough_script { R"JS(
export async function fetchIt(input, idhan) {
    const response = await idhan.request({url: input.url});
    globalThis.__lastStatus = response.status;
}
)JS" };

void runOnce( Harness& harness, const std::string& url )
{
	const auto session { harness.session( url ) };
	REQUIRE( session->submit( url ).has_value() );
	REQUIRE( waitUntil( [ & ] { return session->idle(); } ) );
}
} // namespace

TEST_CASE( "A redirect chain is followed hop by hop", "[downloader][transport]" )
{
	Harness harness {};

	harness.server.route(
		"/start",
		[ & ]( const test::TestRequest& )
		{
			return test::TestResponse {
				.status = 302,
				.reason = "Found",
				.headers = { { "Location", harness.server.url( "/middle" ) } },
				.body = "redirect body that must not be delivered"
			};
		} );
	harness.server.route(
		"/middle",
		[ & ]( const test::TestRequest& )
		{ return test::TestResponse { .status = 302, .reason = "Found", .headers = { { "Location", "/end" } } }; } );
	harness.server.route( "/end", []( const test::TestRequest& ) { return test::TestResponse { .body = "arrived" }; } );

	harness.parsers.write( "test.js", R"JS(
export async function fetchIt(input, idhan) {
    const response = await idhan.request({url: input.url});
    if (response.body !== "arrived") throw new Error(`body was ${response.body}`);
    if (!response.url.endsWith("/end")) throw new Error(`url was ${response.url}`);
}
)JS" );
	harness.configure( R"([{"export":"fetchIt","path":"/start"}])" );

	runOnce( harness, harness.server.url( "/start" ) );

	idhan::test::checkNoFailures( harness.observer );
	CHECK( harness.server.requestCount() == 3 );
}

TEST_CASE( "Every redirect hop carries the cookies set by the one before it", "[downloader][transport]" )
{
	Harness harness {};

	harness.server.route(
		"/login",
		[ & ]( const test::TestRequest& )
		{
			return test::TestResponse {
				.status = 302,
				.reason = "Found",
				.headers = { { "Set-Cookie", "session=granted; Path=/" }, { "Location", "/guarded" } }
			};
		} );
	harness.server.route(
		"/guarded",
		[]( const test::TestRequest& request )
		{
			const auto cookie { request.headers.find( "cookie" ) };
			return test::TestResponse { .body = cookie == request.headers.end() ? "anonymous" : cookie->second };
		} );

	harness.parsers.write( "test.js", R"JS(
export async function fetchIt(input, idhan) {
    const response = await idhan.request({url: input.url});
    if (response.body !== "session=granted") throw new Error(`cookie did not travel: ${response.body}`);
}
)JS" );
	harness.configure( R"([{"export":"fetchIt","path":"/login"}])" );

	runOnce( harness, harness.server.url( "/login" ) );
	idhan::test::checkNoFailures( harness.observer );
}

TEST_CASE( "A cross-origin redirect strips every sensitive header", "[downloader][transport]" )
{
	test::TestServer target {};
	Harness harness {};

	harness.server.route(
		"/start",
		[ & ]( const test::TestRequest& )
		{
			return test::TestResponse {
				.status = 302, .reason = "Found", .headers = { { "Location", target.url( "/end" ) } }
			};
		} );
	target.route( "/end", []( const test::TestRequest& ) { return test::TestResponse { .body = "ok" }; } );

	harness.parsers.write( "test.js", R"JS(
export async function fetchIt(input, idhan) {
    await idhan.request({
        url: input.url,
        headers: {
            Authorization: "bearer secret",
            "Proxy-Authorization": "proxy secret",
            "X-Api-Key": "api secret"
        }
    });
}
)JS" );
	harness.configure( R"([{"export":"fetchIt","path":"/start"}])" );

	runOnce( harness, harness.server.url( "/start" ) );
	idhan::test::checkNoFailures( harness.observer );

	const auto requests { target.requests() };
	REQUIRE( requests.size() == 1 );
	CHECK_FALSE( requests.front().headers.contains( "authorization" ) );
	CHECK_FALSE( requests.front().headers.contains( "proxy-authorization" ) );
	CHECK_FALSE( requests.front().headers.contains( "x-api-key" ) );
}

TEST_CASE( "A session cookie stays inside its session", "[downloader][transport]" )
{
	Harness harness {};

	harness.server.route(
		"/set",
		[]( const test::TestRequest& )
		{
			return test::TestResponse { .headers = { { "Set-Cookie", "ephemeral=1; Path=/" },
			                                         { "Set-Cookie", "durable=2; Path=/; Max-Age=3600" } } };
		} );
	harness.server.route(
		"/check",
		[]( const test::TestRequest& request )
		{
			const auto cookie { request.headers.find( "cookie" ) };
			return test::TestResponse { .body = cookie == request.headers.end() ? "none" : cookie->second };
		} );

	harness.parsers.write( "test.js", R"JS(
export async function set(input, idhan) {
    await idhan.request({url: input.url});
}

export async function check(input, idhan) {
    const response = await idhan.request({url: input.url});
    globalThis.result = response.body;
    console.log(`cookies: ${response.body}`);
}
)JS" );
	harness.configure( R"([{"export":"set","path":"/set"},{"export":"check","path":"/check"}])" );

	{
		const auto first { harness.session( harness.server.url( "/set" ) ) };
		REQUIRE( first->submit( harness.server.url( "/set" ) ).has_value() );
		REQUIRE( waitUntil( [ & ] { return first->idle(); } ) );
		REQUIRE( first->submit( harness.server.url( "/check" ) ).has_value() );
		REQUIRE( waitUntil( [ & ] { return first->idle(); } ) );
	}

	{
		const auto second { harness.session( harness.server.url( "/check" ) ) };
		REQUIRE( second->submit( harness.server.url( "/check" ) ).has_value() );
		REQUIRE( waitUntil( [ & ] { return second->idle(); } ) );
	}

	idhan::test::checkNoFailures( harness.observer );

	const auto checks { harness.server.requests() };
	std::vector< std::string > seen {};

	for ( const auto& request : checks )
	{
		if ( request.path != "/check" ) continue;

		const auto cookie { request.headers.find( "cookie" ) };
		seen.emplace_back( cookie == request.headers.end() ? "" : cookie->second );
	}

	REQUIRE( seen.size() == 2 );
	CHECK( seen[ 0 ].contains( "ephemeral=1" ) );
	CHECK( seen[ 0 ].contains( "durable=2" ) );
	CHECK_FALSE( seen[ 1 ].contains( "ephemeral" ) );
	CHECK( seen[ 1 ].contains( "durable=2" ) );
}

TEST_CASE( "A lane reuses one connection across requests", "[downloader][transport]" )
{
	Harness harness {};
	harness.server.route( "/page", []( const test::TestRequest& ) { return test::TestResponse { .body = "ok" }; } );

	harness.parsers.write( "test.js", R"JS(
export async function fetchIt(input, idhan) {
    for (let index = 0; index < 5; ++index) {
        await idhan.request({url: input.url});
    }
}
)JS" );
	harness.configure( R"([{"export":"fetchIt","path":"/page"}])" );

	runOnce( harness, harness.server.url( "/page" ) );

	idhan::test::checkNoFailures( harness.observer );
	CHECK( harness.server.requestCount() == 5 );
	CHECK( harness.server.acceptedConnections() == 1 );
}

TEST_CASE( "A normal dispatch publishes its lane state", "[downloader][transport]" )
{
	Harness harness {};
	harness.server.route( "/page", []( const test::TestRequest& ) { return test::TestResponse { .body = "ok" }; } );
	harness.parsers.write( "test.js", passthrough_script );
	harness.configure( R"([{"export":"fetchIt","path":"/page"}])", {}, R"({"requests":1,"seconds":5})" );

	runOnce( harness, harness.server.url( "/page" ) );

	const auto snapshots { harness.lane_observer.snapshots() };
	REQUIRE_FALSE( snapshots.empty() );
	const LaneSnapshot& lane { snapshots.back() };
	CHECK( lane.key == "127.0.0.1" );
	CHECK( lane.throttled );
	CHECK( lane.rate_requests == 1 );
	CHECK( lane.rate_seconds == 5 );
	CHECK( lane.shards == 1 );
}

TEST_CASE( "A 429 retries after Retry-After without settling the request", "[downloader][transport]" )
{
	Harness harness {};
	std::atomic_int limited_requests {};
	std::mutex arrivals_mutex {};
	std::vector< std::chrono::steady_clock::time_point > arrivals {};

	harness.server.route(
		"/limited",
		[ & ]( const test::TestRequest& )
		{
			const int attempt { limited_requests.fetch_add( 1 ) };

			{
				const std::scoped_lock lock { arrivals_mutex };
				arrivals.emplace_back( std::chrono::steady_clock::now() );
			}

			if ( attempt != 0 ) return test::TestResponse { .body = "recovered" };

			return test::TestResponse {
				.status = 429, .reason = "Too Many Requests", .headers = { { "Retry-After", "1" } }, .body = "slow down"
			};
		} );
	harness.server.route( "/fine", []( const test::TestRequest& ) { return test::TestResponse { .body = "ok" }; } );

	harness.parsers.write( "test.js", R"JS(
export async function fetchIt(input, idhan) {
    const response = await idhan.request({url: input.url});
    if (input.url.endsWith("/limited") && (response.status !== 200 || response.body !== "recovered")) {
        throw new Error(`429 attempt escaped as ${response.status}: ${response.body}`);
    }
}
)JS" );
	harness.configure(
		R"([{"export":"fetchIt","path":"/limited"},{"export":"fetchIt","path":"/fine"}])",
		DownloaderConfig { .lane_error_backoff = std::chrono::seconds::zero() } );

	runOnce( harness, harness.server.url( "/limited" ) );
	CHECK( limited_requests.load() == 2 );

	{
		const std::scoped_lock lock { arrivals_mutex };
		REQUIRE( arrivals.size() == 2 );
		CHECK( arrivals[ 1 ] - arrivals[ 0 ] >= std::chrono::milliseconds { 900 } );
	}

	const auto backedOff = [ & ]
	{
		const auto snapshots { harness.context->laneSnapshots() };
		return std::ranges::any_of( snapshots, []( const LaneSnapshot& lane ) { return lane.backed_off; } );
	};

	REQUIRE( backedOff() );

	runOnce( harness, harness.server.url( "/fine" ) );
	idhan::test::checkNoFailures( harness.observer );
	CHECK( backedOff() );

	harness.context->resetAllBackoff();
	CHECK_FALSE( backedOff() );
}

TEST_CASE( "A 429 holds every queued request for the Retry-After", "[downloader][transport]" )
{
	Harness harness {};
	std::atomic_int attempts {};
	std::mutex arrivals_mutex {};
	std::vector< std::chrono::steady_clock::time_point > arrivals {};

	harness.server.route(
		"/page",
		[ & ]( const test::TestRequest& )
		{
			const int attempt { attempts.fetch_add( 1 ) };

			{
				const std::scoped_lock lock { arrivals_mutex };
				arrivals.emplace_back( std::chrono::steady_clock::now() );
			}

			if ( attempt != 0 ) return test::TestResponse { .body = "ok" };

			return test::TestResponse {
				.status = 429, .reason = "Too Many Requests", .headers = { { "Retry-After", "2" } }, .body = "slow down"
			};
		} );

	harness.parsers.write( "test.js", R"JS(
export async function fetchIt(input, idhan) {
    const response = await idhan.request({url: input.url});
    if (response.status !== 200) throw new Error(`request ended as ${response.status}`);
}
)JS" );
	// The lane keeps its 30 second error backoff, so only the header can release it this fast.
	harness.configure(
		R"([{"export":"fetchIt","path":"/page"}])", DownloaderConfig {}, R"({"requests":4,"seconds":1})" );

	const auto session { harness.session( harness.server.url( "/page" ) ) };
	const auto started { std::chrono::steady_clock::now() };

	for ( int index = 0; index < 3; ++index ) REQUIRE( session->submit( harness.server.url( "/page" ) ).has_value() );

	REQUIRE( waitUntil( [ & ] { return session->idle(); } ) );
	const auto elapsed { std::chrono::steady_clock::now() - started };

	idhan::test::checkNoFailures( harness.observer );
	CHECK( attempts.load() == 4 );
	CHECK( elapsed < std::chrono::seconds { 10 } );

	{
		const std::scoped_lock lock { arrivals_mutex };
		REQUIRE( arrivals.size() == 4 );
		CHECK( arrivals[ 1 ] - arrivals[ 0 ] >= std::chrono::milliseconds { 1900 } );
	}
}

TEST_CASE( "A streamed import survives an exponential 429 retry", "[downloader][transport]" )
{
	Harness harness {};
	std::atomic_int requests {};

	harness.server.route(
		"/file",
		[ & ]( const test::TestRequest& )
		{
			if ( requests.fetch_add( 1 ) == 0 )
				return test::TestResponse { .status = 429, .reason = "Too Many Requests", .body = "slow down" };

			return test::TestResponse { .headers = { { "Content-Type", "image/png" } }, .body = "file bytes" };
		} );

	harness.parsers.write( "test.js", R"JS(
export function fetchIt(input, idhan) {
    idhan.import({request: {url: input.url}, filename: "retry.png"});
}
)JS" );
	harness.configure(
		R"([{"export":"fetchIt","path":"/file"}])",
		DownloaderConfig { .lane_error_backoff = std::chrono::seconds::zero() },
		R"({"requests":10,"seconds":1})" );

	runOnce( harness, harness.server.url( "/file" ) );

	idhan::test::checkNoFailures( harness.observer );
	CHECK( requests.load() == 2 );
	CHECK( harness.imports.aborted.load() == 0 );
	REQUIRE( harness.imports.stored.size() == 1 );
	CHECK( harness.imports.stored.front().second == "file bytes" );
}

TEST_CASE( "A large body streams into the sink without touching the in-memory ceiling", "[downloader][transport]" )
{
	Harness harness {};
	const std::string payload( 4u * 1024 * 1024, 'x' );

	harness.server.route(
		"/big",
		[ & ]( const test::TestRequest& )
		{ return test::TestResponse { .headers = { { "Content-Type", "video/mp4" } }, .body = payload }; } );
	harness.server.route( "/page", []( const test::TestRequest& ) { return test::TestResponse { .body = "ok" }; } );

	harness.parsers.write( "test.js", R"JS(
export async function fetchIt(input, idhan) {
    const base = new URL(input.url);
    idhan.import({request: {url: new URL("/big", base).href}, filename: "clip.mp4"});
}
)JS" );

	harness.configure( R"([{"export":"fetchIt","path":"/page"}])", DownloaderConfig { .max_response_bytes = 4096 } );

	runOnce( harness, harness.server.url( "/page" ) );

	idhan::test::checkNoFailures( harness.observer );
	REQUIRE( harness.imports.stored.size() == 1 );
	CHECK( harness.imports.stored.front().second.size() == payload.size() );
	REQUIRE( harness.imports.metadata.size() == 1 );
	CHECK( harness.imports.metadata.front().content_type == "video/mp4" );
	CHECK( harness.imports.metadata.front().size == payload.size() );
}

TEST_CASE( "An oversized page body is refused rather than buffered", "[downloader][transport]" )
{
	Harness harness {};
	const std::string payload( 64u * 1024, 'y' );

	harness.server.route(
		"/huge", [ & ]( const test::TestRequest& ) { return test::TestResponse { .body = payload }; } );

	harness.parsers.write( "test.js", passthrough_script );
	harness.configure( R"([{"export":"fetchIt","path":"/huge"}])", DownloaderConfig { .max_response_bytes = 4096 } );

	runOnce( harness, harness.server.url( "/huge" ) );

	REQUIRE( harness.observer.failures.size() == 1 );
	CHECK( harness.observer.failures.front().second.contains( "exceeds" ) );
	CHECK( harness.imports.stored.empty() );
}

TEST_CASE( "An import that answers with an error status stores nothing", "[downloader][transport]" )
{
	Harness harness {};

	harness.server.route(
		"/missing",
		[]( const test::TestRequest& )
		{
			return test::TestResponse {
				.status = 404,
				.reason = "Not Found",
				.headers = { { "Content-Type", "text/html" } },
				.body = "<html>no such file</html>"
			};
		} );
	harness.server.route( "/page", []( const test::TestRequest& ) { return test::TestResponse { .body = "ok" }; } );

	harness.parsers.write( "test.js", R"JS(
export function fetchIt(input, idhan) {
    const base = new URL(input.url);
    idhan.import({request: {url: new URL("/missing", base).href}, filename: "gone.png"});
}
)JS" );
	harness.configure( R"([{"export":"fetchIt","path":"/page"}])" );

	runOnce( harness, harness.server.url( "/page" ) );

	CHECK( harness.imports.stored.empty() );
	CHECK( harness.imports.aborted.load() == 1 );

	REQUIRE( harness.observer.import_failures.size() == 1 );
	CHECK( harness.observer.import_failures.front().second.contains( "404" ) );
}

TEST_CASE( "An HTML body that fails to parse as JSON is quoted back", "[downloader][transport]" )
{
	Harness harness {};

	harness.server.route(
		"/api",
		[]( const test::TestRequest& )
		{
			return test::TestResponse {
				.status = 403,
				.reason = "Forbidden",
				.headers = { { "Content-Type", "text/html; charset=utf-8" } },
				.body = "<html>\n  <body>Access denied</body>\n</html>"
			};
		} );

	harness.parsers.write( "test.js", R"JS(
export async function fetchIt(input, idhan) {
    await idhan.request({url: input.url, responseType: "json"});
}
)JS" );
	harness.configure( R"([{"export":"fetchIt","path":"/api"}])" );

	runOnce( harness, harness.server.url( "/api" ) );

	REQUIRE( harness.observer.failures.size() == 1 );

	const std::string& error { harness.observer.failures.front().second };
	CHECK( error.contains( "text/html" ) );
	CHECK( error.contains( "<html> <body>Access denied</body> </html>" ) );
}

TEST_CASE( "An import that lands on a redirect still stores only the final bytes", "[downloader][transport]" )
{
	Harness harness {};

	harness.server.route(
		"/download",
		[]( const test::TestRequest& )
		{
			return test::TestResponse {
				.status = 302, .reason = "Found", .headers = { { "Location", "/real" } }, .body = "NOT THE FILE"
			};
		} );
	harness.server.route(
		"/real",
		[]( const test::TestRequest& )
		{ return test::TestResponse { .headers = { { "Content-Type", "image/jpeg" } }, .body = "JPEGDATA" }; } );
	harness.server.route( "/page", []( const test::TestRequest& ) { return test::TestResponse { .body = "ok" }; } );

	harness.parsers.write( "test.js", R"JS(
export async function fetchIt(input, idhan) {
    const base = new URL(input.url);
    idhan.import({request: {url: new URL("/download", base).href}});
}
)JS" );
	harness.configure( R"([{"export":"fetchIt","path":"/page"}])" );

	runOnce( harness, harness.server.url( "/page" ) );

	idhan::test::checkNoFailures( harness.observer );
	REQUIRE( harness.imports.stored.size() == 1 );
	CHECK( harness.imports.stored.front().second == "JPEGDATA" );
	CHECK( harness.imports.aborted.load() == 0 );

	REQUIRE( harness.observer.imports.size() == 1 );
	CHECK( harness.observer.imports.front().content_type == "image/jpeg" );
}

TEST_CASE( "A failed request stops its lane until the backoff passes", "[downloader][transport]" )
{
	Harness harness {};
	std::mutex arrivals_mutex {};
	std::vector< std::chrono::steady_clock::time_point > arrivals {};

	const auto record = [ & ]
	{
		const std::scoped_lock lock { arrivals_mutex };
		arrivals.emplace_back( std::chrono::steady_clock::now() );
	};

	harness.server.route(
		"/broken",
		[ & ]( const test::TestRequest& )
		{
			record();
			return test::TestResponse { .status = 500, .reason = "Internal Server Error", .body = "boom" };
		} );
	harness.server.route(
		"/after",
		[ & ]( const test::TestRequest& )
		{
			record();
			return test::TestResponse { .body = "ok" };
		} );

	harness.parsers.write( "test.js", R"JS(
export async function fetchIt(input, idhan) {
    const base = new URL(input.url);
    await idhan.request({url: new URL("/broken", base).href});
    await idhan.request({url: new URL("/after", base).href});
}
)JS" );
	harness.configure(
		R"([{"export":"fetchIt","path":"/page"}])",
		DownloaderConfig { .lane_error_backoff = std::chrono::seconds { 2 } } );

	runOnce( harness, harness.server.url( "/page" ) );

	idhan::test::checkNoFailures( harness.observer );

	const std::scoped_lock lock { arrivals_mutex };
	REQUIRE( arrivals.size() == 2 );
	CHECK( arrivals[ 1 ] - arrivals[ 0 ] >= std::chrono::milliseconds { 1900 } );
}

TEST_CASE( "A rate limited lane spaces its requests out", "[downloader][transport]" )
{
	Harness harness {};
	harness.server.route( "/page", []( const test::TestRequest& ) { return test::TestResponse { .body = "ok" }; } );

	harness.parsers.write( "test.js", R"JS(
export async function fetchIt(input, idhan) {
    await idhan.request({url: input.url});
}
)JS" );
	harness.configure(
		R"([{"export":"fetchIt","path":"/page"}])", DownloaderConfig {}, R"({"requests":1,"seconds":1})" );

	const auto session { harness.session( harness.server.url( "/page" ) ) };
	const auto started { std::chrono::steady_clock::now() };

	for ( int index = 0; index < 3; ++index ) REQUIRE( session->submit( harness.server.url( "/page" ) ).has_value() );

	REQUIRE( waitUntil( [ & ] { return session->idle(); } ) );
	const auto elapsed { std::chrono::steady_clock::now() - started };

	idhan::test::checkNoFailures( harness.observer );
	CHECK( harness.server.requestCount() == 3 );
	CHECK( elapsed >= std::chrono::milliseconds { 1900 } );
}

TEST_CASE( "A rate limited lane spaces import requests before dispatch", "[downloader][transport]" )
{
	Harness harness {};
	std::mutex arrivals_mutex {};
	std::vector< std::chrono::steady_clock::time_point > arrivals {};

	const auto file = [ & ]( const test::TestRequest& )
	{
		const std::scoped_lock lock { arrivals_mutex };
		arrivals.emplace_back( std::chrono::steady_clock::now() );
		return test::TestResponse { .body = "file" };
	};

	harness.server.route( "/file/1", file );
	harness.server.route( "/file/2", file );
	harness.server.route( "/file/3", file );

	harness.parsers.write( "test.js", R"JS(
export async function fetchIt(input, idhan) {
    const base = new URL(input.url);
    for (const id of [1, 2, 3]) {
        idhan.import({request: {url: new URL(`/file/${id}`, base).href}});
    }
}
)JS" );
	harness.configure(
		R"([{"export":"fetchIt","path":"/page"}])", DownloaderConfig {}, R"({"requests":1,"seconds":1})" );

	runOnce( harness, harness.server.url( "/page" ) );
	idhan::test::checkNoFailures( harness.observer );

	const std::scoped_lock lock { arrivals_mutex };
	REQUIRE( arrivals.size() == 3 );

	for ( std::size_t index { 1 }; index < arrivals.size(); ++index )
	{
		CHECK( arrivals[ index ] - arrivals[ index - 1 ] >= std::chrono::milliseconds { 900 } );
	}
}

TEST_CASE( "Clearing a lane's backoff releases its queue at once", "[downloader][transport]" )
{
	Harness harness {};
	harness.server.route( "/one", []( const test::TestRequest& ) { return test::TestResponse { .body = "ok" }; } );
	harness.server.route( "/two", []( const test::TestRequest& ) { return test::TestResponse { .body = "ok" }; } );

	harness.parsers.write( "test.js", R"JS(
export async function fetchIt(input, idhan) {
    await idhan.request({url: input.url});
}
)JS" );
	harness.configure(
		R"([{"export":"fetchIt","path":"/one"},{"export":"fetchIt","path":"/two"}])",
		DownloaderConfig {},
		R"({"requests":1,"seconds":30})" );

	const auto session { harness.session( harness.server.url( "/one" ) ) };
	REQUIRE( session->submit( harness.server.url( "/one" ) ).has_value() );
	REQUIRE( session->submit( harness.server.url( "/two" ) ).has_value() );

	REQUIRE( waitUntil( [ & ] { return harness.server.requestCount() >= 1; } ) );
	CHECK( harness.server.requestCount() == 1 );

	harness.context->resetAllBackoff();

	REQUIRE( waitUntil( [ & ] { return harness.server.requestCount() >= 2; }, std::chrono::seconds { 3 } ) );
	REQUIRE( waitUntil( [ & ] { return session->idle(); } ) );
	idhan::test::checkNoFailures( harness.observer );
}
