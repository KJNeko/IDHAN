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

	const bool started { waitUntil( [ & ] { return request_started.load(); } ) };
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

TEST_CASE( "A URL no class accepts is refused at submit", "[downloader][session]" )
{
	Harness harness {};
	harness.parsers.write( "test.js", "export async function post() {}\n" );
	harness.configure( R"([{"export":"post","path":"/post"}])" );

	const auto session { harness.session( "https://example.com/" ) };
	const auto submitted { session->submit( "https://example.com/nope" ) };

	REQUIRE_FALSE( submitted.has_value() );
	CHECK( submitted.error().contains( "No URL class accepts" ) );
	CHECK( harness.context->validate( "https://example.com/nope" ).has_value() == false );
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
