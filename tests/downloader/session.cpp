#include <IDHANDownloader/DownloaderContext.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <format>
#include <functional>
#include <thread>

#include "Harness.hpp"

using namespace idhan;
using namespace idhan::downloader;
using idhan::test::Harness;
using idhan::test::waitUntil;

TEST_CASE( "A parser runs, requests, follows and imports", "[downloader][session]" )
{
	Harness harness {};

	harness.server.route(
		"/data",
		[]( const test::TestRequest& )
		{
			return test::TestResponse {
				.headers = { { "Content-Type", "application/json" } }, .body = R"({"ok":true})"
			};
		} );
	harness.server.route(
		"/file",
		[]( const test::TestRequest& )
		{ return test::TestResponse { .headers = { { "Content-Type", "image/png" } }, .body = "PNGBYTES" }; } );
	harness.server.route( "/child", []( const test::TestRequest& ) { return test::TestResponse { .body = "child" }; } );

	harness.parsers.write( "test.js", R"JS(
export async function gallery(input, idhan) {
    const base = new URL(input.url);
    const response = await idhan.request({url: new URL("/data", base).href, responseType: "json"});
    if (response.body.ok !== true) throw new Error("request result was not propagated");

    idhan.follow({url: new URL("/child", base).href});
    idhan.follow({url: new URL("/child", base).href});

    idhan.import({
        request: {url: new URL("/file", base).href},
        filename: "fixture.png",
        tags: ["fixture"],
    });
}

export async function child(input, idhan) {
    await idhan.request({url: input.url});
}
)JS" );

	harness.configure( R"([{"export":"gallery","path":"/gallery"},{"export":"child","path":"/child"}])" );

	const auto session { harness.session( harness.server.url( "/gallery" ) ) };
	const auto submitted { session->submit( harness.server.url( "/gallery" ) ) };
	REQUIRE( submitted.has_value() );

	REQUIRE( waitUntil( [ & ] { return session->idle(); } ) );

	idhan::test::checkNoFailures( harness.observer );
	CHECK( harness.observer.started.size() == 2 );

	const auto queued { std::ranges::count_if(
		harness.observer.follows, []( const auto& entry ) { return entry.second == FollowStatus::QUEUED; } ) };
	const auto deduped { std::ranges::count_if(
		harness.observer.follows,
		[]( const auto& entry ) { return entry.second == FollowStatus::ALREADY_EXPLORED; } ) };
	CHECK( queued == 1 );
	CHECK( deduped == 1 );

	REQUIRE( harness.imports.stored.size() == 1 );
	CHECK( harness.imports.stored.front().second == "PNGBYTES" );
	REQUIRE( harness.observer.imports.size() == 1 );
	CHECK( harness.observer.imports.front().content_type == "image/png" );
	CHECK( harness.observer.imports.front().size == 8 );
}

TEST_CASE( "A session's counters total the work it actually did", "[downloader][session][diagnostics]" )
{
	Harness harness {};

	harness.server.route(
		"/data",
		[]( const test::TestRequest& )
		{
			return test::TestResponse {
				.headers = { { "Content-Type", "application/json" } }, .body = R"({"ok":true})"
			};
		} );
	harness.server.route(
		"/file",
		[]( const test::TestRequest& )
		{ return test::TestResponse { .headers = { { "Content-Type", "image/png" } }, .body = "PNGBYTES" }; } );
	harness.server.route( "/child", []( const test::TestRequest& ) { return test::TestResponse { .body = "child" }; } );

	harness.parsers.write( "test.js", R"JS(
export async function gallery(input, idhan) {
    const base = new URL(input.url);
    await idhan.request({url: new URL("/data", base).href, responseType: "json"});

    idhan.follow({url: new URL("/child", base).href});
    idhan.follow({url: new URL("/child", base).href});

    idhan.import({
        request: {url: new URL("/file", base).href},
        filename: "fixture.png",
        tags: ["fixture"],
    });
}

export async function child(input, idhan) {
    await idhan.request({url: input.url});
}
)JS" );

	harness.configure( R"([{"export":"gallery","path":"/gallery"},{"export":"child","path":"/child"}])" );

	const auto session { harness.session( harness.server.url( "/gallery" ) ) };
	REQUIRE( session->submit( harness.server.url( "/gallery" ) ).has_value() );
	REQUIRE( waitUntil( [ & ] { return session->idle(); } ) );

	const SessionSnapshot snapshot { session->snapshot() };

	CHECK( snapshot.root_url == harness.server.url( "/gallery" ) );
	CHECK( snapshot.queued == 0 );
	CHECK( snapshot.in_flight_requests == 0 );
	CHECK( snapshot.counters.work_started == 2 );
	CHECK( snapshot.counters.work_completed == 2 );
	CHECK( snapshot.counters.work_failed == 0 );
	CHECK( snapshot.counters.requests == 2 );
	CHECK( snapshot.counters.imported == 1 );
	CHECK( snapshot.counters.import_bytes == 8 );
	CHECK( snapshot.counters.import_failed == 0 );
	CHECK( snapshot.counters.follows_queued == 1 );
	CHECK( snapshot.counters.follows_already_explored == 1 );

	CHECK( snapshot.counters.work_started == harness.observer.started.size() );
	CHECK( snapshot.counters.imported == harness.observer.imports.size() );

	const auto kinds = [ & ]( const SessionEventKind kind )
	{
		return std::ranges::count_if(
			snapshot.events, [ kind ]( const SessionEvent& event ) { return event.kind == kind; } );
	};

	CHECK( kinds( SessionEventKind::STARTED ) == 2 );
	CHECK( kinds( SessionEventKind::IMPORTED ) == 1 );
	CHECK( kinds( SessionEventKind::FOLLOWED ) == 2 );
	CHECK( kinds( SessionEventKind::FINISHED ) >= 1 );

	CHECK( session->snapshot( snapshot.event_sequence ).events.empty() );
}

TEST_CASE( "A stalled session reports the work and request holding it up", "[downloader][session][diagnostics]" )
{
	Harness harness {};

	std::atomic_bool release { false };

	harness.server.route(
		"/slow",
		[ & ]( const test::TestRequest& )
		{
			while ( !release.load() ) std::this_thread::sleep_for( std::chrono::milliseconds { 10 } );

			return test::TestResponse { .body = "slow" };
		} );

	harness.parsers.write( "test.js", R"JS(
export async function slow(input, idhan) {
    await idhan.request({url: `${input.url}`});
}
)JS" );

	harness.configure( R"([{"export":"slow","path":"/slow"}])" );

	const auto session { harness.session( harness.server.url( "/slow" ) ) };
	REQUIRE( session->submit( harness.server.url( "/slow" ) ).has_value() );

	const bool observed { waitUntil( [ & ] { return !session->snapshot().requests.empty(); } ) };
	release.store( true );
	REQUIRE( observed );

	REQUIRE( waitUntil( [ & ] { return session->idle(); } ) );

	const SessionSnapshot drained { session->snapshot() };
	CHECK( drained.requests.empty() );
	CHECK( drained.work.empty() );
	CHECK( drained.counters.requests == 1 );
}

TEST_CASE( "Cancelling a session stops and drains its active imports", "[downloader][session]" )
{
	Harness harness {};
	std::atomic_bool release {};

	struct ReleaseOnExit
	{
		std::atomic_bool& flag;

		~ReleaseOnExit() { flag.store( true ); }
	} release_on_exit { release };

	harness.server.route(
		"/slow",
		[ & ]( const test::TestRequest& )
		{
			while ( !release.load() ) std::this_thread::sleep_for( std::chrono::milliseconds { 5 } );

			return test::TestResponse { .body = "file" };
		} );

	harness.parsers.write( "test.js", R"JS(
export function slow(input, idhan) {
    idhan.import({request: {url: input.url}});
}
)JS" );
	harness.configure( R"([{"export":"slow","path":"/slow"}])" );

	const auto session { harness.session( harness.server.url( "/slow" ) ) };
	REQUIRE( session->submit( harness.server.url( "/slow" ) ).has_value() );
	REQUIRE( waitUntil( [ & ] { return harness.server.requestCount() == 1; } ) );

	session->cancel();
	session->wait();

	CHECK( session->idle() );
	CHECK( harness.imports.aborted.load() == 1 );
	CHECK( harness.imports.stored.empty() );
}

TEST_CASE( "Scripts awaiting a request run concurrently", "[downloader][session]" )
{
	Harness harness {};

	std::atomic_size_t inside { 0 };
	std::atomic_size_t peak { 0 };

	harness.server.route(
		"/slow",
		[ & ]( const test::TestRequest& )
		{
			const std::size_t now { inside.fetch_add( 1 ) + 1 };
			std::size_t seen { peak.load() };

			while ( now > seen && !peak.compare_exchange_weak( seen, now ) )
			{}

			std::this_thread::sleep_for( std::chrono::milliseconds { 300 } );
			inside.fetch_sub( 1 );
			return test::TestResponse { .body = "slow" };
		} );

	harness.parsers.write( "test.js", R"JS(
export async function slow(input, idhan) {
    await idhan.request({url: `${input.url}`});
}
)JS" );

	harness.configure( R"([{"export":"slow","path":"/slow"}])" );

	const auto session { harness.session( harness.server.url( "/slow" ) ) };

	for ( int index = 0; index < 4; ++index )
	{
		const auto submitted { session->submit( harness.server.url( "/slow" ) ) };
		REQUIRE( submitted.has_value() );
	}

	REQUIRE( waitUntil( [ & ] { return session->idle(); } ) );

	idhan::test::checkNoFailures( harness.observer );
	CHECK( harness.observer.started.size() == 4 );
	CHECK( peak.load() == 4 );
}

TEST_CASE( "A completed script does not wait for its requests or followed children", "[downloader][session]" )
{
	Harness harness {};
	std::atomic_bool request_started {};
	std::atomic_bool release_request {};

	harness.server.route(
		"/slow",
		[ & ]( const test::TestRequest& )
		{
			request_started.store( true );
			while ( !release_request.load() ) std::this_thread::sleep_for( std::chrono::milliseconds { 5 } );
			return test::TestResponse { .body = "done" };
		} );

	harness.parsers.write( "test.js", R"JS(
export function parent(input, idhan) {
    const slow = new URL("/slow", input.url).href;
    void idhan.request({url: slow});
    idhan.follow({url: slow});
}

export async function child(input, idhan) {
    await idhan.request({url: input.url});
}
)JS" );
	harness.configure( R"([{"export":"parent","path":"/parent"},{"export":"child","path":"/slow"}])" );

	const auto session { harness.session( harness.server.url( "/parent" ) ) };
	const auto submitted { session->submit( harness.server.url( "/parent" ) ) };
	REQUIRE( submitted.has_value() );

	const bool started { waitUntil( [ & ]() noexcept { return request_started.load(); } ) };
	const bool parent_completed {
		started
		&& waitUntil( [ & ] { return harness.observer.hasCompleted( *submitted ); }, std::chrono::seconds { 1 } )
	};
	release_request.store( true );

	REQUIRE( waitUntil( [ & ] { return session->idle(); } ) );
	REQUIRE( started );
	CHECK( parent_completed );
	CHECK( harness.observer.completed.size() == 2 );
}

TEST_CASE( "The in-flight request budget bounds how many scripts start", "[downloader][session]" )
{
	Harness harness {};

	std::atomic_size_t inside { 0 };
	std::atomic_size_t peak { 0 };

	harness.server.route(
		"/slow",
		[ & ]( const test::TestRequest& )
		{
			const std::size_t now { inside.fetch_add( 1 ) + 1 };
			std::size_t seen { peak.load() };

			while ( now > seen && !peak.compare_exchange_weak( seen, now ) )
			{}

			std::this_thread::sleep_for( std::chrono::milliseconds { 150 } );
			inside.fetch_sub( 1 );
			return test::TestResponse { .body = "slow" };
		} );

	harness.parsers.write( "test.js", R"JS(
export async function slow(input, idhan) {
    await idhan.request({url: `${input.url}`});
}
)JS" );

	harness.configure( R"([{"export":"slow","path":"/slow"}])", DownloaderConfig { .session_inflight_requests = 2 } );

	const auto session { harness.session( harness.server.url( "/slow" ) ) };

	for ( int index = 0; index < 6; ++index )
	{
		const auto submitted { session->submit( harness.server.url( "/slow" ) ) };
		REQUIRE( submitted.has_value() );
	}

	REQUIRE( waitUntil( [ & ] { return session->idle(); } ) );

	idhan::test::checkNoFailures( harness.observer );
	CHECK( harness.observer.started.size() == 6 );
	CHECK( peak.load() <= 2 );
}

TEST_CASE( "A script that throws fails only its own work", "[downloader][session]" )
{
	Harness harness {};

	harness.server.route( "/ok", []( const test::TestRequest& ) { return test::TestResponse { .body = "fine" }; } );

	harness.parsers.write( "test.js", R"JS(
export async function boom() {
    throw new Error("parser exploded");
}

export async function fine(input, idhan) {
    await idhan.request({url: `${input.url}`});
}
)JS" );

	harness.configure( R"([{"export":"boom","path":"/boom"},{"export":"fine","path":"/ok"}])" );

	const auto session { harness.session( harness.server.url( "/boom" ) ) };
	REQUIRE( session->submit( harness.server.url( "/boom" ) ).has_value() );
	REQUIRE( session->submit( harness.server.url( "/ok" ) ).has_value() );

	REQUIRE( waitUntil( [ & ] { return session->idle(); } ) );

	REQUIRE( harness.observer.failures.size() == 1 );
	CHECK( harness.observer.failures.front().second.contains( "parser exploded" ) );
	CHECK( harness.observer.requests.size() == 1 );
}

TEST_CASE( "Non-HTTP URLs without a parser are refused at submit", "[downloader][session]" )
{
	Harness harness {};
	harness.parsers.write( "test.js", "export async function post() {}\n" );
	harness.configure( R"([{"export":"post","path":"/post"}])" );

	const auto session { harness.session( "https://example.com/" ) };
	const auto submitted { session->submit( "ftp://example.com/nope" ) };

	REQUIRE_FALSE( submitted.has_value() );
	CHECK( submitted.error().contains( "No URL class accepts" ) );
	CHECK( harness.context->validate( "ftp://example.com/nope" ).has_value() == false );
}

TEST_CASE( "A secret reaches the script", "[downloader][session]" )
{
	Harness harness {};
	harness.secrets.values.emplace( "site.token", "hunter2" );
	harness.server.route(
		"/auth",
		[]( const test::TestRequest& request )
		{
			const auto found { request.headers.find( "authorization" ) };
			return test::TestResponse { .body = found == request.headers.end() ? "none" : found->second };
		} );

	harness.parsers.write( "test.js", R"JS(
export async function auth(input, idhan) {
    const token = idhan.secret("site.token");
    const missing = idhan.secret("site.absent");
    if (missing !== null) throw new Error("absent secrets must be null");
    const response = await idhan.request({url: input.url, headers: {authorization: token}});
    if (response.body !== "hunter2") throw new Error(`header did not arrive: ${response.body}`);
}
)JS" );

	harness.configure( R"([{"export":"auth","path":"/auth"}])" );

	const auto session { harness.session( harness.server.url( "/auth" ) ) };
	REQUIRE( session->submit( harness.server.url( "/auth" ) ).has_value() );
	REQUIRE( waitUntil( [ & ] { return session->idle(); } ) );

	idhan::test::checkNoFailures( harness.observer );
}

TEST_CASE( "Sensitive query values are absent from session diagnostics", "[downloader][session][diagnostics]" )
{
	Harness harness {};
	std::atomic_bool release {};

	struct ReleaseOnExit
	{
		std::atomic_bool& flag;

		~ReleaseOnExit() { flag.store( true ); }
	} release_on_exit { release };

	harness.server.route(
		"/auth",
		[ & ]( const test::TestRequest& )
		{
			while ( !release.load() ) std::this_thread::sleep_for( std::chrono::milliseconds { 5 } );

			return test::TestResponse { .body = "ok" };
		} );

	harness.parsers.write( "test.js", R"JS(
export async function auth(input, idhan) {
    const url = `${input.url}?api_key=top-secret&user_id=private-user&safe=visible`;
    await idhan.request({url, sensitiveQuery: ["api_key", "user_id"]});
}
)JS" );
	harness.configure( R"([{"export":"auth","path":"/auth"}])" );

	const auto session { harness.session( harness.server.url( "/auth" ) ) };
	REQUIRE( session->submit( harness.server.url( "/auth" ) ).has_value() );
	REQUIRE( waitUntil( [ & ] { return !session->snapshot().requests.empty(); } ) );

	const SessionSnapshot pending { session->snapshot() };
	REQUIRE( pending.requests.size() == 1 );
	CHECK_FALSE( pending.requests.front().url.contains( "top-secret" ) );
	CHECK_FALSE( pending.requests.front().url.contains( "private-user" ) );
	CHECK( pending.requests.front().url.contains( "safe=visible" ) );
	CHECK( pending.requests.front().url.contains( "%3Credacted%3E" ) );

	release.store( true );
	REQUIRE( waitUntil( [ & ] { return session->idle(); } ) );
	REQUIRE( harness.observer.requests.size() == 1 );
	CHECK_FALSE( harness.observer.requests.front().url.contains( "top-secret" ) );
	CHECK_FALSE( harness.observer.requests.front().url.contains( "private-user" ) );
}

TEST_CASE( "A request started by failing module evaluation is abandoned safely", "[downloader][session]" )
{
	Harness harness {};

	harness.server.route(
		"/slow",
		[]( const test::TestRequest& )
		{
			std::this_thread::sleep_for( std::chrono::milliseconds { 300 } );
			return test::TestResponse { .body = "late" };
		} );

	const std::string script { std::format(
		"void idhan.request({{url: '{}'}});\nwhile (true) {{}}\nexport function post() {{}}\n",
		harness.server.url( "/slow" ) ) };
	harness.parsers.write( "test.js", script );
	harness.configure(
		R"([{"export":"post","path":"/slow"}])",
		DownloaderConfig { .script_burst_timeout = std::chrono::milliseconds { 100 } } );

	const auto session { harness.session( harness.server.url( "/slow" ) ) };
	REQUIRE( session->submit( harness.server.url( "/slow" ) ).has_value() );
	REQUIRE( waitUntil( [ & ] { return session->idle(); } ) );
	REQUIRE( harness.observer.failures.size() == 1 );
	CHECK( harness.observer.failures.front().second.contains( "interrupted" ) );
	CHECK( session->snapshot().requests.empty() );

	REQUIRE( waitUntil(
		[ & ]
		{
			const auto snapshots { harness.context->laneSnapshots() };
			return !snapshots.empty() && snapshots.back().in_flight == 0;
		} ) );
}

TEST_CASE( "Closing concurrently with submissions drains runner-held work", "[downloader][session]" )
{
	Harness harness {};
	harness.parsers.write( "test.js", "export function post() {}\n" );
	harness.configure( R"([{"export":"post","path":"/post"}])" );

	const auto session { harness.session( harness.server.url( "/post" ) ) };
	std::atomic_bool begin {};

	std::jthread submitter {
		[ & ]
		{
			while ( !begin.load() )
			{}

			for ( int index = 0; index < 1000; ++index ) (void)session->submit( harness.server.url( "/post" ) );
		}
	};

	begin.store( true );
	session->close();
	submitter.join();

	REQUIRE( waitUntil( [ & ] { return session->outstanding() == 0; } ) );
	CHECK( session->snapshot().queued == 0 );
}

TEST_CASE( "A header carrying a newline never reaches the transport", "[downloader][session]" )
{
	Harness harness {};
	harness.server.route( "/x", []( const test::TestRequest& ) { return test::TestResponse {}; } );

	harness.parsers.write( "test.js", R"JS(
export async function post(input, idhan) {
    await idhan.request({url: input.url, headers: {"x-bad": "one\r\nInjected: yes"}});
}
)JS" );

	harness.configure( R"([{"export":"post","path":"/x"}])" );

	const auto session { harness.session( harness.server.url( "/x" ) ) };
	REQUIRE( session->submit( harness.server.url( "/x" ) ).has_value() );
	REQUIRE( waitUntil( [ & ] { return session->idle(); } ) );

	REQUIRE( harness.observer.failures.size() == 1 );
	CHECK( harness.observer.failures.front().second.contains( "Invalid request header" ) );
	CHECK( harness.server.requestCount() == 0 );
}

TEST_CASE(
	"Unscoped parser tags use downloader tags and single-domain selectors are retired",
	"[downloader][session][tag-domain]" )
{
	Harness harness {};
	std::string manifest {};
	std::string domain_option {};
	bool invalid {};

	SECTION( "No domain uses downloader tags" )
	{}
	SECTION( "An obsolete manifest domain does not change the default" )
	{
		manifest = R"(export const manifest = {tagDomain: "parser tags"};)";
	}
	SECTION( "Single-domain import selectors are rejected" )
	{
		domain_option = R"(tagDomain: "other tags",)";
		invalid = true;
	}

	harness.imports.reusable = ImportResult { .record_id = 42 };
	harness.parsers.write(
		"test.js",
		std::format(
			R"JS(
{}
export function post(input, idhan) {{
    idhan.import({{{} request: {{url: new URL("/file.png", input.url).href}}, tags: ["test"]}});
}}
)JS",
			manifest,
			domain_option ) );
	harness.configure( R"([{"export":"post","path":"/post"}])" );
	const auto session { harness.session( harness.server.url( "/post" ) ) };
	REQUIRE( session->submit( harness.server.url( "/post" ) ).has_value() );
	session->wait();

	if ( invalid )
	{
		REQUIRE( harness.observer.failures.size() == 1 );
		CHECK( harness.observer.failures.front().second.contains( "tagDomain is no longer supported" ) );
		CHECK( harness.imports.reuse_requests.empty() );
		return;
	}

	idhan::test::checkNoFailures( harness.observer );
	REQUIRE( harness.imports.reuse_requests.size() == 1 );
	const auto& options { harness.imports.reuse_requests.front().options };
	CHECK_FALSE( options.isMember( "tagDomain" ) );
	REQUIRE( options[ "tags" ].isObject() );
	CHECK( options[ "tags" ].size() == 1 );
	CHECK( options[ "tags" ][ "downloader tags" ][ 0 ].asString() == "test" );
}

TEST_CASE( "A parser sends tags to multiple explicit domains", "[downloader][session][tag-domain]" )
{
	Harness harness {};
	harness.imports.reusable = ImportResult { .record_id = 42 };
	harness.parsers.write( "test.js", R"JS(
export function post(input, idhan) {
    idhan.import({
        request: {url: new URL("/file.png", input.url).href},
        tags: {pixiv: ["artist:example", "landscape"], curated: ["favorite"], "mydomain.com": ["shared"]},
    });
}
)JS" );
	harness.configure( R"([{"export":"post","path":"/post"}])" );
	const auto session { harness.session( harness.server.url( "/post" ) ) };
	REQUIRE( session->submit( harness.server.url( "/post" ) ).has_value() );
	session->wait();
	idhan::test::checkNoFailures( harness.observer );
	REQUIRE( harness.imports.reuse_requests.size() == 1 );
	const auto& options { harness.imports.reuse_requests.front().options };
	CHECK_FALSE( options.isMember( "tagDomain" ) );
	REQUIRE( options[ "tags" ].isObject() );
	CHECK( options[ "tags" ].size() == 3 );
	CHECK( options[ "tags" ][ "pixiv" ][ 0 ].asString() == "artist:example" );
	CHECK( options[ "tags" ][ "pixiv" ][ 1 ].asString() == "landscape" );
	CHECK( options[ "tags" ][ "curated" ][ 0 ].asString() == "favorite" );
	CHECK( options[ "tags" ][ "mydomain.com" ][ 0 ].asString() == "shared" );
}

TEST_CASE( "Invalid tag domain maps are rejected before dispatch", "[downloader][session][tag-domain]" )
{
	Harness harness {};
	std::string tags {};
	SECTION( "Blank domain names" )
	{
		tags = R"({"  ":["tag"]})";
	}
	SECTION( "NUL in domain names" )
	{
		tags = R"({"bad\u0000domain":["tag"]})";
	}
	SECTION( "Domain tags must be arrays" )
	{
		tags = R"({"pixiv":"tag"})";
	}
	SECTION( "Domain tags must be strings" )
	{
		tags = R"({"pixiv":["tag",42]})";
	}
	SECTION( "Unscoped arrays must also contain strings" )
	{
		tags = R"(["tag",42])";
	}
	SECTION( "Tags cannot be a scalar" )
	{
		tags = "42";
	}

	harness.parsers.write(
		"test.js",
		std::format(
			R"JS(
export function post(input, idhan) {{
    idhan.import({{request: {{url: new URL("/file.png", input.url).href}}, tags: {}}});
}}
)JS",
			tags ) );
	harness.configure( R"([{"export":"post","path":"/post"}])" );
	const auto session { harness.session( harness.server.url( "/post" ) ) };
	REQUIRE( session->submit( harness.server.url( "/post" ) ).has_value() );
	session->wait();
	REQUIRE( harness.observer.failures.size() == 1 );
	CHECK( harness.observer.failures.front().second.contains( "idhan.import tags" ) );
	CHECK( harness.imports.reuse_requests.empty() );
}

TEST_CASE( "Direct media URLs import without a parser", "[downloader][session][direct]" )
{
	Harness harness {};
	std::atomic_size_t requests {};
	harness.server.route(
		"/file.png",
		[ &requests ]( const test::TestRequest& )
		{
			++requests;
			return test::TestResponse { .headers = { { "Content-Type", "image/png" } }, .body = "IMAGE" };
		} );
	harness.parsers.write( "test.js", "export function page() {}" );
	harness.configure( R"([{"export":"page","path":"/page"}])" );

	bool force_session {};
	bool force_context {};
	SECTION( "Unknown files download" )
	{}
	SECTION( "Known files reuse the existing record without HTTP" )
	{
		harness.imports.reusable = ImportResult { .record_id = 42, .note = "Already in the database" };
	}
	SECTION( "A runtime force flag bypasses reuse" )
	{
		harness.imports.reusable = ImportResult { .record_id = 42 };
		DownloaderConfig config {};
		config.force_download = true;
		force_context = true;
		harness.context.reset();
		harness.configure( R"([{"export":"page","path":"/page"}])", config );
	}
	SECTION( "Lookup failures prevent dispatch and are reported" )
	{
		harness.imports.reuse_error = "lookup failed";
	}

	SECTION( "The session force flag bypasses reuse" )
	{
		force_session = true;
		harness.imports.reusable = ImportResult { .record_id = 42 };
	}
	const auto session { harness.context->createSession(
		SessionOptions { .force_download = force_session,
		                 .root_url = harness.server.url( "/file.png" ),
		                 .observer = &harness.observer } ) };
	REQUIRE( session->submit( harness.server.url( "/file.png" ) ).has_value() );
	session->wait();
	CHECK( harness.observer.failures.empty() );
	if ( force_session || force_context )
	{
		CHECK( harness.imports.reuse_requests.empty() );
		CHECK( requests == 1 );
		REQUIRE( harness.imports.stored.size() == 1 );
		CHECK( harness.observer.import_failures.empty() );
		return;
	}

	REQUIRE( harness.imports.reuse_requests.size() == 1 );
	if ( harness.imports.reuse_error )
	{
		CHECK( requests == 0 );
		CHECK( harness.observer.import_failures.size() == 1 );
		CHECK( harness.observer.imports.empty() );
		return;
	}

	CHECK( harness.observer.import_failures.empty() );
	REQUIRE( harness.observer.imports.size() == 1 );
	if ( harness.imports.reusable )
	{
		CHECK( requests == 0 );
		CHECK( harness.imports.stored.empty() );
		CHECK( harness.observer.imports.front().record_id == 42 );
	}
	else
	{
		CHECK( requests == 1 );
		REQUIRE( harness.imports.stored.size() == 1 );
		CHECK( harness.imports.stored.front().second == "IMAGE" );
	}
}

TEST_CASE( "Automatic file inspection rejects pages and structured responses", "[downloader][session][direct]" )
{
	Harness harness {};
	test::TestResponse response {};
	SECTION( "HTML" )
	{
		response.headers = { { "Content-Type", "text/html" } };
		response.body = "<html>login</html>";
	}
	SECTION( "JSON" )
	{
		response.headers = { { "Content-Type", "application/json" } };
		response.body = "{}";
	}
	SECTION( "XML" )
	{
		response.headers = { { "Content-Type", "application/xml" } };
		response.body = "<?xml version='1.0'?><page/>";
	}
	SECTION( "HTML labelled as binary" )
	{
		response.headers = { { "Content-Type", "application/octet-stream" } };
		response.body = "<!DOCTYPE html><html/>";
	}
	SECTION( "Markup with a byte order mark labelled as binary" )
	{
		response.headers = { { "Content-Type", "application/octet-stream" } };
		response.body = "\xEF\xBB\xBF <!--comment--><html/>";
	}
	SECTION( "JSON labelled as binary" )
	{
		response.headers = { { "Content-Type", "application/octet-stream" } };
		response.body = " {\"url\":\"file.png\"}";
	}
	SECTION( "Missing MIME" )
	{
		response.body = "unknown";
	}
	harness.server.route( "/download", [ response ]( const test::TestRequest& ) { return response; } );
	harness.parsers.write( "test.js", "export function page() {}" );
	harness.configure( R"([{"export":"page","path":"/page"}])" );
	const auto session { harness.session( harness.server.url( "/download" ) ) };
	REQUIRE( session->submit( harness.server.url( "/download" ) ).has_value() );
	session->wait();
	CHECK( harness.imports.stored.empty() );
	REQUIRE( harness.observer.import_failures.size() == 1 );
	CHECK( harness.observer.import_failures.front().second.contains( "single file" ) );
}

TEST_CASE( "Parsers can force an individual import and retain its metadata", "[downloader][session][direct]" )
{
	Harness harness {};
	std::atomic_size_t requests {};
	harness.server.route(
		"/download",
		[ &requests ]( const test::TestRequest& )
		{
			++requests;
			return test::TestResponse { .headers = { { "Content-Type", "image/png" } }, .body = "IMAGE" };
		} );
	harness.parsers.write( "test.js", R"JS(
export function page(input, idhan) {
    idhan.import({request: {url: new URL("/download", input.url).href},
        tags: ["test"], forceDownload: true});
}
)JS" );
	harness.configure( R"([{"export":"page","path":"/page"}])" );
	harness.imports.reusable = ImportResult { .record_id = 42 };
	SECTION( "Force download" )
	{}
	SECTION( "Reuse carries metadata" )
	{
		harness.parsers.write( "test.js", R"JS(
export function page(input, idhan) {
    idhan.import({request: {url: new URL("/download", input.url).href},
        tags: ["test"], urls: [{url: input.url, type: "post"}]});
}
)JS" );
	}
	const auto session { harness.session( harness.server.url( "/page" ) ) };
	REQUIRE( session->submit( harness.server.url( "/page" ) ).has_value() );
	session->wait();
	CHECK( harness.observer.import_failures.empty() );
	REQUIRE( harness.observer.imports.size() == 1 );
	if ( harness.imports.reuse_requests.empty() )
	{
		CHECK( requests == 1 );
		CHECK( harness.imports.stored.size() == 1 );
	}
	else
	{
		CHECK( requests == 0 );
		CHECK( harness.observer.imports.front().record_id == 42 );
		CHECK(
			harness.imports.reuse_requests.front().options[ "tags" ][ "downloader tags" ][ 0 ].asString() == "test" );
		CHECK(
			harness.imports.reuse_requests.front().options[ "urls" ][ 0 ][ "url" ].asString()
		    == harness.server.url( "/page" ) );
	}
}

TEST_CASE( "Parsers declare URL types through setters, follows and imports", "[downloader][session][url-types]" )
{
	Harness harness {};
	harness.parsers.write( "test.js", R"JS(
export function gallery(input, idhan) {
    const post = new URL("/post", input.url).href;
    idhan.setUrlType({url: input.url, type: "gallery"});
    idhan.follow({url: post});
    idhan.follow({url: post, type: "post"});
    idhan.import({request: {url: new URL("/file", input.url).href}, urls: [
        {url: post, type: "post"},
        {url: new URL("/file", input.url).href, type: "file"},
        {url: new URL("/source", input.url).href, type: "unknown"},
    ]});
}
export function post() {}
)JS" );
	harness.configure( R"([{"export":"gallery","path":"/gallery"},{"export":"post","path":"/post"}])" );
	harness.imports.reusable = ImportResult { .record_id = 42 };
	const auto session { harness.session( harness.server.url( "/gallery" ) ) };
	REQUIRE( session->submit( harness.server.url( "/gallery" ) ).has_value() );
	session->wait();
	CHECK( harness.observer.failures.empty() );
	CHECK( harness.observer.import_failures.empty() );
	CHECK( harness.observer.completed.size() == 2 );
	REQUIRE( harness.observer.url_types.size() == 5 );
	CHECK(
		harness.observer.url_types[ 0 ] == std::pair { harness.server.url( "/gallery" ), std::string { "gallery" } } );
	CHECK( harness.observer.url_types[ 1 ] == std::pair { harness.server.url( "/post" ), std::string { "post" } } );
	CHECK( harness.observer.url_types[ 2 ] == std::pair { harness.server.url( "/post" ), std::string { "post" } } );
	CHECK( harness.observer.url_types[ 3 ] == std::pair { harness.server.url( "/file" ), std::string { "file" } } );
	CHECK(
		harness.observer.url_types[ 4 ] == std::pair { harness.server.url( "/source" ), std::string { "unknown" } } );
	REQUIRE( harness.observer.follows.size() == 2 );
	CHECK( harness.observer.follows.back().second == FollowStatus::ALREADY_EXPLORED );
	REQUIRE( harness.observer.imports.size() == 1 );
	CHECK( harness.observer.imports.front().record_id == 42 );
}

TEST_CASE( "Invalid parser URL types fail before any work is dispatched", "[downloader][session][url-types]" )
{
	Harness harness {};
	std::string call {};
	SECTION( "Unknown type" )
	{
		call = R"(idhan.setUrlType({url: input.url, type: "image"}))";
	}
	SECTION( "Nonstring type" )
	{
		call = R"(idhan.setUrlType({url: input.url, type: 42}))";
	}
	SECTION( "Null type" )
	{
		call = R"(idhan.setUrlType({url: input.url, type: null}))";
	}
	SECTION( "Missing type" )
	{
		call = R"(idhan.setUrlType({url: input.url}))";
	}
	SECTION( "Missing URL" )
	{
		call = R"(idhan.setUrlType({type: "post"}))";
	}
	SECTION( "Relative URL" )
	{
		call = R"(idhan.setUrlType({url: "relative", type: "post"}))";
	}
	SECTION( "Follow type" )
	{
		call = R"(idhan.follow({url: input.url, type: "invalid"}))";
	}
	SECTION( "Null follow type" )
	{
		call = R"(idhan.follow({url: input.url, type: null}))";
	}
	SECTION( "Import types validate before applying any declarations" )
	{
		call = R"(idhan.import({request: {url: input.url}, urls: [
            {url: input.url, type: "post"}, {url: input.url, type: "invalid"}] }))";
	}
	SECTION( "Nonarray import URLs" )
	{
		call = R"(idhan.import({request: {url: input.url}, urls: "post"}))";
	}
	SECTION( "Invalid import declaration" )
	{
		call = R"(idhan.import({request: {url: input.url}, urls: ["post"]}))";
	}
	harness.parsers.write( "test.js", "export function gallery(input, idhan) { " + call + "; }" );
	harness.configure( R"([{"export":"gallery","path":"/gallery"}])" );
	const auto session { harness.session( harness.server.url( "/gallery" ) ) };
	REQUIRE( session->submit( harness.server.url( "/gallery" ) ).has_value() );
	session->wait();
	CHECK( harness.observer.failures.size() == 1 );
	CHECK( harness.observer.started.size() == 1 );
	CHECK( harness.observer.url_types.empty() );
	CHECK( harness.observer.follows.empty() );
	CHECK( harness.observer.imports.empty() );
	CHECK( harness.imports.reuse_requests.empty() );
}

TEST_CASE( "URL type persistence failures reach the parser", "[downloader][session][url-types]" )
{
	Harness harness {};
	std::string call {};
	SECTION( "Setter" )
	{
		call = R"(idhan.setUrlType({url: input.url, type: "gallery"}))";
	}
	SECTION( "Follow" )
	{
		call = R"(idhan.follow({url: input.url, type: "post"}))";
	}
	SECTION( "Import" )
	{
		call = R"(idhan.import({request: {url: input.url}, urls: [{url: input.url, type: "post"}]}))";
	}
	harness.observer.url_type_error = "URL classification database unavailable";
	harness.parsers.write( "test.js", "export function gallery(input, idhan) { " + call + "; }" );
	harness.configure( R"([{"export":"gallery","path":"/gallery"}])" );
	const auto session { harness.session( harness.server.url( "/gallery" ) ) };
	REQUIRE( session->submit( harness.server.url( "/gallery" ) ).has_value() );
	session->wait();
	REQUIRE( harness.observer.failures.size() == 1 );
	CHECK( harness.observer.failures.front().second.contains( "URL classification database unavailable" ) );
	CHECK( harness.observer.url_types.empty() );
	CHECK( harness.observer.follows.empty() );
	CHECK( harness.imports.reuse_requests.empty() );
}

TEST_CASE(
	"Follow comparisons normalize queries while requests retain signatures",
	"[downloader][session][url-comparison]" )
{
	Harness harness {};
	harness.parsers.write( "test.js", R"JS(
export function gallery(input, idhan) {
    for (const query of ["b=2&a=1&is=stable&ex=old&hm=old", "hm=new&is=stable&a=1&b=2&ex=new&", "a=1&b=2&is=different&ex=last&hm=last"]) {
        idhan.follow({url: new URL("/file.png?" + query, input.url).href});
    }
    idhan.follow({url: new URL("/gallery?hm=changed&ex=changed", input.url).href});
}
)JS" );
	harness.configureRaw(
		R"({"urlClasses":[{"name":"test","script":"test.js","hosts":["127.0.0.1"],"requestRate":false,"routes":[{"export":"gallery","path":"/gallery"}],"ephemeralQuery":["ex","hm"]}]})" );
	harness.server.route(
		"/file.png",
		[]( const test::TestRequest& )
		{ return test::TestResponse { .headers = { { "Content-Type", "image/png" } }, .body = "IMAGE" }; } );
	const auto root { harness.server.url( "/gallery?ex=root&hm=root" ) };
	const auto session { harness.session( root ) };
	REQUIRE( session->submit( root ).has_value() );
	session->wait();
	test::checkNoFailures( harness.observer );
	CHECK( harness.observer.import_failures.empty() );
	CHECK( harness.server.requestCount() == 2 );
	REQUIRE( harness.observer.follows.size() == 4 );
	CHECK( harness.observer.follows[ 0 ].second == FollowStatus::QUEUED );
	CHECK( harness.observer.follows[ 1 ].second == FollowStatus::ALREADY_EXPLORED );
	CHECK( harness.observer.follows[ 2 ].second == FollowStatus::QUEUED );
	CHECK( harness.observer.follows[ 3 ].second == FollowStatus::ALREADY_EXPLORED );
	REQUIRE( harness.imports.reuse_requests.size() == 2 );
	for ( const auto& request : harness.imports.reuse_requests )
	{
		CHECK( request.ephemeral_query == std::vector< std::string > { "ex", "hm" } );
		CHECK( request.url.contains( "ex=" ) );
		CHECK( request.url.contains( "hm=" ) );
	}
	for ( const auto& request : harness.server.requests() )
	{
		CHECK( request.target.contains( "ex=" ) );
		CHECK( request.target.contains( "hm=" ) );
	}
}

TEST_CASE(
	"Storage sorting applies to imports and declarations without changing HTTP",
	"[downloader][session][url-storage]" )
{
	Harness harness {};
	harness.parsers.write( "test.js", R"JS(
export function page(input, idhan) {
    idhan.setUrlType({url: new URL("/post?z=2&a=1", input.url).href, type:"post"});
    idhan.follow({url: new URL("/child?z=2&a=1", input.url).href});
    idhan.import({request:{url: new URL("/file?z=2&ex=sig%2braw&a=1&hm=hash", input.url).href},
        urls:[{url:"https://nosort.example/post?z=2&a=1"}],
        discoveredUrls:["https://sort.example/post?z=2&a=1"]});
}
export function child() {}
)JS" );
	std::string setting {};
	bool enabled { true };
	bool reused {};
	SECTION( "Omitted settings enable sorting" )
	{}
	SECTION( "Explicit true enables sorting" )
	{
		setting = ",\"sortQueryParameters\":true";
	}
	SECTION( "Explicit false preserves storage order" )
	{
		setting = ",\"sortQueryParameters\":false";
		enabled = false;
	}
	SECTION( "Reused imports receive the sorted storage URL" )
	{
		reused = true;
		harness.imports.reusable = ImportResult { .record_id = 42 };
	}
	harness.configureRaw(
		std::format(
			R"({{"urlClasses":[
		{{"name":"test","script":"test.js","hosts":["127.0.0.1"],"requestRate":false,
		"routes":[{{"export":"page","path":"/page"}},{{"export":"child","path":"/child"}}],
		"ephemeralQuery":["ex","hm"]{}}},
		{{"name":"nosort","script":"test.js","hosts":["nosort.example"],
		"routes":[{{"export":"page"}}],"sortQueryParameters":false}}
	]}})",
			setting ) );
	harness.server.route(
		"/file",
		[]( const test::TestRequest& )
		{
			return test::TestResponse {
				.status = 302, .headers = { { "Location", "/final?z=2&ex=sig%2braw&a=1&hm=hash" } }
			};
		} );
	harness.server.route(
		"/final",
		[]( const test::TestRequest& )
		{ return test::TestResponse { .headers = { { "Content-Type", "image/png" } }, .body = "IMAGE" }; } );
	const auto root { harness.server.url( "/page?z=2&a=1" ) };
	const auto session { harness.session( root ) };
	REQUIRE( session->submit( root ).has_value() );
	session->wait();
	test::checkNoFailures( harness.observer );
	CHECK( harness.observer.import_failures.empty() );
	REQUIRE( harness.imports.reuse_requests.size() == 1 );
	const auto& stored { harness.imports.reuse_requests.front() };
	CHECK(
		stored.url
	    == harness.server.url(
			enabled ? "/file?a=1&ex=sig%2braw&hm=hash&z=2" : "/file?z=2&ex=sig%2braw&a=1&hm=hash" ) );
	CHECK( stored.source_url == harness.server.url( enabled ? "/page?a=1&z=2" : "/page?z=2&a=1" ) );
	CHECK( stored.options[ "urls" ][ 0 ][ "url" ].asString() == "https://nosort.example/post?z=2&a=1" );
	CHECK( stored.options[ "discoveredUrls" ][ 0 ].asString() == "https://sort.example/post?a=1&z=2" );
	if ( reused )
	{
		CHECK( harness.imports.metadata.empty() );
		CHECK( harness.server.requestCount() == 0 );
		return;
	}

	REQUIRE( harness.imports.metadata.size() == 1 );
	CHECK(
		harness.imports.metadata.front().final_url
	    == harness.server.url(
			enabled ? "/final?a=1&ex=sig%2braw&hm=hash&z=2" : "/final?z=2&ex=sig%2braw&a=1&hm=hash" ) );
	REQUIRE( harness.observer.url_types.size() == 1 );
	CHECK(
		harness.observer.url_types.front().first == harness.server.url( enabled ? "/post?a=1&z=2" : "/post?z=2&a=1" ) );
	REQUIRE( harness.observer.follows.size() == 1 );
	CHECK(
		harness.observer.follows.front().first == harness.server.url( enabled ? "/child?a=1&z=2" : "/child?z=2&a=1" ) );
	const auto requests { harness.server.requests() };
	REQUIRE( requests.size() == 2 );
	CHECK( requests[ 0 ].target == "/file?z=2&ex=sig%2braw&a=1&hm=hash" );
	CHECK( requests[ 1 ].target == "/final?z=2&ex=sig%2braw&a=1&hm=hash" );
}
