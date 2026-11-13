#include <IDHANDownloader/DownloaderContext.hpp>
#include <catch2/catch_test_macros.hpp>
#include <folly/coro/Baton.h>
#include <folly/coro/BlockingWait.h>

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

namespace idhan::test
{
class DiagnosticBlockingObserver final : public SessionObserver
{
  public:

	std::atomic_bool entered {};
	folly::coro::Baton release {};

	folly::coro::Task< std::expected< void, std::string > > onImportQueued( WorkInfo ) override
	{
		entered.store( true );
		co_await release;
		co_return std::expected< void, std::string > {};
	}

	folly::coro::Task< std::expected< void, std::string > > onUrlTypeSet( std::string, std::string ) override
	{
		entered.store( true );
		co_await release;
		co_return std::expected< void, std::string > {};
	}
};
} // namespace idhan::test

TEST_CASE( "Parser diagnostics distinguish import and host waits from HTTP", "[downloader][diagnostics]" )
{
	Harness harness {};
	test::DiagnosticBlockingObserver observer;
	bool importing {};
	SECTION( "Host persistence" )
	{}
	SECTION( "Import persistence" )
	{
		importing = true;
	}
	harness.server.route(
		"/file",
		[]( const test::TestRequest& )
		{ return test::TestResponse { .headers = { { "Content-Type", "image/png" } }, .body = "PNG" }; } );
	harness.parsers.write(
		"test.js",
		importing ? R"JS(
export async function page(input, host) { await host.queueImport({request: {url: input.url}}).importFinished; }
)JS" :
					R"JS(
export async function page(input, host) { await host.setUrlType({url: input.url, type: "post"}); }
)JS" );
	harness.configure( R"([{"export":"page","path":"/file"}])", DownloaderConfig { .io_threads = 1 } );
	const auto session { harness.context->createSession( SessionOptions { .observer = &observer } ) };
	REQUIRE( session->submit( harness.server.url( "/file" ) ).has_value() );
	const bool observed { waitUntil(
		[ & ]
		{
			const auto snapshot { session->snapshot() };
			return observer.entered.load()
		        && ( importing ? snapshot.parsers.waiting_imports : snapshot.parsers.waiting_host ) == 1;
		} ) };
	const auto waiting { session->snapshot() };
	observer.release.post();
	folly::coro::blockingWait( session->wait() );
	folly::coro::blockingWait( harness.context->shutdown() );
	REQUIRE( observed );
	CHECK( waiting.parsers.waiting_requests == 0 );
	CHECK( waiting.parsers.ready == 0 );
	CHECK( waiting.waiting_requests == 0 );
	CHECK( waiting.waiting_parsers == 0 );
	CHECK_FALSE( waiting.script_executing );
	REQUIRE( waiting.work.size() == 1 );
	CHECK(
		waiting.work.front().activity
	    == ( importing ? ParserActivity::WAITING_IMPORT : ParserActivity::WAITING_HOST ) );
	REQUIRE( waiting.requests.size() == 1 );
	CHECK( waiting.requests.front().host == !importing );
	CHECK( waiting.requests.front().import == importing );
	CHECK( session->snapshot().work.empty() );
}

TEST_CASE(
	"Unobserved imports release their parser while native work remains pending",
	"[downloader][session][import-detached]" )
{
	Harness harness {};
	test::DiagnosticBlockingObserver observer;
	harness.server.route(
		"/file",
		[]( const test::TestRequest& )
		{ return test::TestResponse { .headers = { { "Content-Type", "image/png" } }, .body = "PNG" }; } );
	harness.parsers.write( "test.js", R"JS(
export function page(input, host) {
    const result = host.queueImport({request: {url: input.url}});
    const descriptor = Object.getOwnPropertyDescriptor(Object.getPrototypeOf(result), 'importFinished');
    if (typeof descriptor?.get !== 'function' || descriptor.set !== undefined || result.then !== undefined)
        throw new Error('Queued imports must return a handle without a promise');
}
)JS" );
	harness.configure( R"([{"export":"page","path":"/file"}])", DownloaderConfig { .io_threads = 1 } );
	const auto session { harness.context->createSession( SessionOptions { .observer = &observer } ) };
	REQUIRE( session->submit( harness.server.url( "/file" ) ).has_value() );
	const bool observed { waitUntil(
		[ & ]
		{
			const auto snapshot { session->snapshot() };
			return observer.entered.load() && snapshot.work.empty() && snapshot.requests.size() == 1;
		} ) };
	const auto waiting { session->snapshot() };
	observer.release.post();
	folly::coro::blockingWait( session->wait() );
	folly::coro::blockingWait( harness.context->shutdown() );
	REQUIRE( observed );
	CHECK( waiting.parsers.waiting_imports == 0 );
	CHECK_FALSE( waiting.idle );
	CHECK( waiting.counters.work_failed == 0 );
	CHECK( session->snapshot().counters.imported == 1 );
}

TEST_CASE( "Import status remains available after native completion", "[downloader][session][import-status]" )
{
	Harness harness {};
	bool failed {};
	SECTION( "Stored record" )
	{
		harness.imports.stored_record = 42;
	}
	SECTION( "Failed lookup" )
	{
		failed = true;
		harness.imports.lookup_error = "controlled lookup failure";
	}

	std::atomic_bool release {};

	struct ReleaseOnExit
	{
		std::atomic_bool& flag;

		~ReleaseOnExit() { flag.store( true ); }
	} release_on_exit { release };

	harness.server.route(
		"/gate",
		[ &release ]( const test::TestRequest& )
		{
			while ( !release.load() ) std::this_thread::sleep_for( std::chrono::milliseconds { 5 } );

			return test::TestResponse { .body = "ready" };
		} );
	harness.parsers.write( "test.js", R"JS(
export async function page(input, host) {
    const handle = host.queueImport({request: {url: new URL('/file', input.url).href}});
    await host.request({url: new URL('/gate', input.url).href});
    const status = handle.importFinished;
    if (status !== handle.importFinished) throw new Error('importFinished must reuse its promise');
    let failure;
    try {
        const result = await status;
        if (!result.existing) throw new Error('Lost completed import result');
    } catch (error) {
        failure = error;
    }
    if (input.url.includes('failed')) {
        if (!String(failure).includes('controlled lookup failure')) throw new Error('Lost import failure');
    } else if (failure !== undefined) {
        throw failure;
    }
}
)JS" );
	harness.configure( R"([{"export":"page","path":"/page"}])" );
	const auto session { harness.session( "" ) };
	REQUIRE( session->submit( harness.server.url( failed ? "/page?failed" : "/page" ) ).has_value() );
	const bool completed { waitUntil(
		[ & ]
		{
			const auto snapshot { session->snapshot() };
			const auto finished { failed ? snapshot.counters.import_failed : snapshot.counters.imported };
			return finished == 1 && snapshot.requests.size() == 1 && !snapshot.requests.front().import;
		} ) };
	release.store( true );
	folly::coro::blockingWait( session->wait() );
	folly::coro::blockingWait( harness.context->shutdown() );
	REQUIRE( completed );
	CHECK( session->snapshot().counters.work_failed == 0 );
	CHECK( harness.server.requestCount() == 1 );
	CHECK( harness.imports.lookup_requests.size() == 1 );
}

TEST_CASE( "Import status rejects when persistence fails", "[downloader][session][import-status]" )
{
	Harness harness {};
	harness.imports.finish_error = "controlled persistence failure";
	harness.server.route( "/file", []( const test::TestRequest& ) { return test::TestResponse { .body = "file" }; } );
	harness.parsers.write( "test.js", R"JS(
export async function page(input, host) {
    const handle = host.queueImport({request: {url: input.url}});
    try {
        await handle.importFinished;
    } catch (error) {
        if (!String(error).includes('controlled persistence failure')) throw error;
        return;
    }
    throw new Error('Import unexpectedly succeeded');
}
)JS" );
	harness.configure( R"([{"export":"page","path":"/file"}])" );
	const auto session { harness.session( "" ) };
	REQUIRE( session->submit( harness.server.url( "/file" ) ).has_value() );
	folly::coro::blockingWait( session->wait() );
	CHECK( session->snapshot().counters.import_failed == 1 );
	CHECK( session->snapshot().counters.work_failed == 0 );
}

TEST_CASE(
	"An unsupported file does not stop queued imports or later gallery pages",
	"[downloader][session][import-failure]" )
{
	Harness harness {};
	const auto unsupported_url { harness.server.url( "/unsupported.bmp" ) };
	harness.imports.finish_errors.emplace(
		unsupported_url, "Downloaded file has an unrecognised MIME type: image/x-ms-bmp" );
	for ( const auto* path : { "/first.png", "/unsupported.bmp", "/last.png" } )
		harness.server.route(
			path,
			[]( const test::TestRequest& request )
			{ return test::TestResponse { .headers = { { "Content-Type", "image/png" } }, .body = request.path }; } );

	std::atomic_bool page_after_failure {};
	harness.server.route(
		"/next-page",
		[ &harness, &page_after_failure ]( const test::TestRequest& )
		{
			// Delay the next page until the unsupported file's sink has actually failed.
			const bool failed {
				waitUntil( [ &harness ] { return harness.imports.aborted.load() > 0; }, std::chrono::seconds { 2 } )
			};
			page_after_failure.store( failed );
			return test::TestResponse { .status = failed ? 200 : 500, .body = "next page" };
		} );
	harness.parsers.write( "test.js", R"JS(
export async function page(input, host) {
    host.queueImport({request: {url: new URL('/first.png', input.url).href}});
    host.queueImport({request: {url: new URL('/unsupported.bmp', input.url).href}});
    await host.request({url: new URL('/next-page', input.url).href});
    host.queueImport({request: {url: new URL('/last.png', input.url).href}});
}
)JS" );
	harness.configure( R"([{"export":"page","path":"/page"}])", DownloaderConfig { .lane_concurrency = 4 } );
	const auto session { harness.session( harness.server.url( "/page" ) ) };
	REQUIRE( session->submit( session->rootUrl() ).has_value() );
	folly::coro::blockingWait( session->wait() );
	folly::coro::blockingWait( harness.context->shutdown() );
	const auto snapshot { session->snapshot() };
	CHECK( page_after_failure.load() );
	CHECK_FALSE( snapshot.cancelled );
	CHECK( snapshot.counters.work_failed == 0 );
	CHECK( snapshot.counters.import_failed == 1 );
	CHECK( snapshot.counters.imported == 2 );
	CHECK( harness.observer.failures.empty() );
	REQUIRE( harness.observer.import_failures.size() == 1 );
	CHECK( harness.observer.import_failures.front().second.contains( "unrecognised MIME type" ) );
	REQUIRE( harness.imports.finished_requests.size() == 2 );
	std::vector< std::string > imported_urls;
	for ( const auto& request : harness.imports.finished_requests ) imported_urls.push_back( request.url );
	CHECK( std::ranges::find( imported_urls, harness.server.url( "/first.png" ) ) != imported_urls.end() );
	CHECK( std::ranges::find( imported_urls, harness.server.url( "/last.png" ) ) != imported_urls.end() );
}

TEST_CASE( "Sessions share their domain parser across overlapping invocations", "[downloader][session][parser-reuse]" )
{
	Harness harness {};
	const std::filesystem::path runtime {
		std::filesystem::path { IDHAN_DOWNLOADER_TEST_PARSER_DIR } / "packages/sdk/runtime.js"
	};
	std::filesystem::copy_file( runtime, harness.parsers.root() / "runtime.js" );
	harness.parsers.write( "helper.js", "export const value = 42;" );
	harness.parsers.write( "test.js", R"JS(
import {bindParser} from './runtime.js';
export default class Parser {
    static metadata = {
        formatVersion: 1, id: "reuse", version: 1, sdkVersion: "1.1.0", options: [], discoveries: [], urlClasses: [],
        handlers: [{id: "page", method: "page", parameters: {name: {type: "string"}}}]
    };
    constructor(site) {
        this.site = site;
        this.calls = 0;
        this.configurations = 0;
    }
    async configure() {
        ++this.configurations;
        await this.site.setBasicAuth(this.site.secrets.username ?? "user", this.site.secrets.password ?? "pass");
        await this.site.setCookie("configured", "yes");
    }
    async page(input, session) {
        if (this.configurations !== 1) throw new Error("Parser configured repeatedly");
        const call = ++this.calls;
        const query = `?call=${call}&name=${input.params.name}`;
        await session.request({url: new URL('/start' + query, this.site.domain).href, responseType: 'text'});
        const helper = await import('./helper.js');
        if (helper.value !== 42) throw new Error("Retained parser lost its module loader");
        const response = await session.fetch(new URL('/finish' + query, this.site.domain).href);
        if (await response.text() !== 'ok') throw new Error('Session-bound fetch failed');
    }
}
const dispatch = bindParser(Parser);
export function run(input, host) {
    return dispatch({...input, params: {name: new URL(input.url).searchParams.get('name')}}, host);
}
)JS" );
	harness.server.route( "/start", []( const test::TestRequest& ) { return test::TestResponse { .body = "ok" }; } );
	harness.server.route( "/finish", []( const test::TestRequest& ) { return test::TestResponse { .body = "ok" }; } );
	DownloaderConfig config {};
	config.io_threads = 1;
	harness.configure( R"([{"export":"page","path":"/page"}])", config );
	const auto session { harness.session( "" ) };
	REQUIRE( session->submit( harness.server.url( "/page?name=first" ) ).has_value() );
	folly::coro::blockingWait( session->wait() );
	REQUIRE( session->submit( harness.server.url( "/page?name=second" ) ).has_value() );
	REQUIRE( session->submit( harness.server.url( "/page?name=third" ) ).has_value() );
	folly::coro::blockingWait( session->wait() );
	idhan::test::checkNoFailures( harness.observer );
	const auto requests { harness.server.requests() };
	REQUIRE( requests.size() == 6 );
	std::vector< std::string > starts;
	for ( const auto& request : requests )
	{
		CHECK( request.headers.at( "authorization" ) == "Basic dXNlcjpwYXNz" );
		CHECK( request.headers.at( "cookie" ).contains( "configured=yes" ) );
		if ( request.path == "/start" ) starts.push_back( request.target );
	}
	REQUIRE( starts.size() == 3 );
	std::ranges::sort( starts );
	CHECK( starts[ 0 ] == "/start?call=1&name=first" );
	CHECK( starts[ 1 ].starts_with( "/start?call=2&" ) );
	CHECK( starts[ 2 ].starts_with( "/start?call=3&" ) );
	for ( const auto& request : harness.observer.requests )
	{
		const auto started { std::ranges::find( harness.observer.started, request.work, &WorkInfo::id ) };
		REQUIRE( started != harness.observer.started.end() );
		const auto name { started->url.substr( started->url.find( "name=" ) ) };
		CHECK( request.url.ends_with( name ) );
	}

	// Another session shares the parser instance and its domain state.
	const auto other { harness.session( "" ) };
	REQUIRE( other->submit( harness.server.url( "/page?name=other" ) ).has_value() );
	folly::coro::blockingWait( other->wait() );
	idhan::test::checkNoFailures( harness.observer );
	CHECK( harness.server.requests().back().target == "/finish?call=4&name=other" );

	test::TestServer other_domain;
	other_domain.route( "/start", []( const test::TestRequest& ) { return test::TestResponse { .body = "ok" }; } );
	other_domain.route( "/finish", []( const test::TestRequest& ) { return test::TestResponse { .body = "ok" }; } );
	REQUIRE( session->submit( other_domain.url( "/page?name=domain" ) ).has_value() );
	folly::coro::blockingWait( session->wait() );
	idhan::test::checkNoFailures( harness.observer );
	CHECK( other_domain.requests().back().target == "/finish?call=1&name=domain" );

	Json::Value secrets { Json::objectValue };
	secrets[ "username" ] = "updated";
	secrets[ "password" ] = "credentials";
	harness.secrets.values[ "127.0.0.1" ] = std::move( secrets );
	REQUIRE( session->submit( harness.server.url( "/page?name=updated" ) ).has_value() );
	folly::coro::blockingWait( session->wait() );
	idhan::test::checkNoFailures( harness.observer );
	const auto updated { harness.server.requests().back() };
	CHECK( updated.target == "/finish?call=1&name=updated" );
	CHECK( updated.headers.at( "authorization" ) == "Basic dXBkYXRlZDpjcmVkZW50aWFscw==" );
	REQUIRE( session->submit( harness.server.url( "/page?name=reused" ) ).has_value() );
	folly::coro::blockingWait( session->wait() );
	idhan::test::checkNoFailures( harness.observer );
	CHECK( harness.server.requests().back().target == "/finish?call=2&name=reused" );
}

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

    idhan.queueImport({
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

    idhan.queueImport({
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

	const bool observed { waitUntil( [ & ] { return session->snapshot().parsers.waiting_requests == 1; } ) };
	const auto waiting { session->snapshot() };
	release.store( true );
	REQUIRE( observed );
	CHECK( waiting.parsers.ready == 0 );
	CHECK( waiting.waiting_requests == 1 );
	CHECK( waiting.waiting_parsers == 0 );
	CHECK( waiting.parsers.waiting_host == 0 );
	CHECK_FALSE( waiting.script_executing );
	CHECK( waiting.script_busy_ns > 0 );
	CHECK( waiting.script_bursts > 0 );
	REQUIRE( waiting.work.size() == 1 );
	CHECK( waiting.work.front().activity == ParserActivity::WAITING_REQUEST );
	CHECK( waiting.work.front().pending_requests == 1 );
	REQUIRE( waiting.requests.size() == 1 );
	CHECK_FALSE( waiting.requests.front().host );

	REQUIRE( waitUntil( [ & ] { return session->idle(); } ) );

	const SessionSnapshot drained { session->snapshot() };
	CHECK( drained.requests.empty() );
	CHECK( drained.work.empty() );
	CHECK( drained.parsers.waiting_requests == 0 );
	CHECK_FALSE( drained.script_executing );
	CHECK( drained.counters.requests == 1 );
}

TEST_CASE( "Downloader shutdown drains interrupted work without failures", "[downloader][session][shutdown]" )
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
		[ &release ]( const test::TestRequest& )
		{
			while ( !release.load() ) std::this_thread::sleep_for( std::chrono::milliseconds { 5 } );

			return test::TestResponse { .body = "file" };
		} );
	std::string script;
	SECTION( "Parser requests" )
	{
		script = R"JS(
export async function slow(input, host) {
    await Promise.all([host.request({url: input.url}), host.request({url: input.url})]);
}
)JS";
	}
	SECTION( "Awaited imports" )
	{
		script = R"JS(
export async function slow(input, host) {
    await Promise.all([host.queueImport({request: {url: input.url}}).importFinished, host.queueImport({request: {url: input.url}}).importFinished]);
}
)JS";
	}
	SECTION( "Imports outliving their parser" )
	{
		script = R"JS(
export function slow(input, host) {
    host.queueImport({request: {url: input.url}});
    host.queueImport({request: {url: input.url}});
}
)JS";
	}
	SECTION( "Partial producer stream" )
	{
		script = R"JS(
export async function slow(input, host) {
    await host.importDirect({id: 1, operation: 'open', filename: 'partial.bin', urls: [], tags: {}, metadata: {}});
    await host.importDirect({id: 1, operation: 'write'}, new Uint8Array([1, 2, 3]));
    await Promise.all([host.request({url: input.url}), host.request({url: input.url})]);
}
)JS";
	}
	harness.parsers.write( "test.js", script );
	harness.configure( R"([{"export":"slow","path":"/slow"}])", DownloaderConfig {}, "false", R"({"concurrency":1})" );
	const auto session { harness.session( harness.server.url( "/slow" ) ) };
	REQUIRE( session->submit( harness.server.url( "/slow" ) ).has_value() );
	REQUIRE( waitUntil( [ & ]
	                    { return harness.server.requestCount() == 1 && session->snapshot().requests.size() == 2; } ) );

	folly::coro::blockingWait( harness.context->shutdown() );
	const auto snapshot { session->snapshot() };
	CHECK( snapshot.closed );
	CHECK( snapshot.idle );
	CHECK( snapshot.requests.empty() );
	CHECK( snapshot.work.empty() );
	CHECK( snapshot.counters.work_failed == 0 );
	CHECK( snapshot.counters.requests_failed == 0 );
	CHECK( snapshot.counters.import_failed == 0 );
	CHECK( harness.observer.failures.empty() );
	CHECK( harness.observer.import_failures.empty() );
	CHECK( harness.imports.stored.empty() );
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
    idhan.queueImport({request: {url: input.url}});
}
)JS" );
	harness.configure( R"([{"export":"slow","path":"/slow"}])" );

	const auto session { harness.session( harness.server.url( "/slow" ) ) };
	REQUIRE( session->submit( harness.server.url( "/slow" ) ).has_value() );
	REQUIRE( waitUntil( [ & ] { return harness.server.requestCount() == 1; } ) );

	session->cancel();
	folly::coro::blockingWait( session->wait() );

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

	harness.configure(
		R"([{"export":"slow","path":"/slow"}])",
		DownloaderConfig {},
		R"({"requests":100,"seconds":1,"concurrency":4})" );

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

TEST_CASE(
	"A parser error backs off requests in other sessions like a 429",
	"[downloader][session][parser-rate-limit]" )
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

	harness.configure(
		R"([{"export":"boom","path":"/boom"},{"export":"fine","path":"/ok"}])",
		DownloaderConfig { .lane_error_backoff = std::chrono::seconds { 1 } },
		R"({"requests":3,"seconds":1,"spread":true})" );

	const auto session { harness.session( harness.server.url( "/boom" ) ) };
	const auto started { std::chrono::steady_clock::now() };
	REQUIRE( session->submit( harness.server.url( "/boom" ) ).has_value() );
	REQUIRE( waitUntil( [ & ] { return session->idle(); } ) );
	const auto lanes { harness.context->laneSnapshots() };
	REQUIRE( lanes.size() == 1 );
	CHECK( lanes.front().parser_limited );
	CHECK( lanes.front().parser_error_hits == 1 );
	CHECK( lanes.front().rate_limit_hits == 0 );
	CHECK( lanes.front().rate == ( RequestRate { 3, 1, true } ) );

	const auto other { harness.session( harness.server.url( "/ok" ) ) };
	REQUIRE( other->submit( harness.server.url( "/ok" ) ).has_value() );
	REQUIRE( waitUntil( [ & ] { return other->idle(); } ) );
	CHECK( std::chrono::steady_clock::now() - started >= std::chrono::milliseconds { 900 } );

	REQUIRE( harness.observer.failures.size() == 1 );
	CHECK( harness.observer.failures.front().second.contains( "parser exploded" ) );
	CHECK( harness.observer.requests.size() == 1 );
}

TEST_CASE(
	"Parser startup and execution errors back off without reducing the allowance",
	"[downloader][session][parser-rate-limit]" )
{
	Harness harness {};
	std::string script;
	SECTION( "Module evaluation" )
	{
		script = "throw new Error('module failure'); export function boom() {}";
	}
	SECTION( "Synchronous exception" )
	{
		script = "export function boom() { throw new Error('sync failure'); }";
	}
	SECTION( "Promise rejection" )
	{
		script = "export async function boom() { throw new Error('async failure'); }";
	}
	harness.parsers.write( "test.js", script );
	harness.configure(
		R"([{"export":"boom","path":"/boom"}])",
		DownloaderConfig { .lane_error_backoff = std::chrono::seconds { 2 } },
		R"({"requests":3,"seconds":1})" );
	const auto session { harness.session( harness.server.url( "/boom" ) ) };
	REQUIRE( session->submit( harness.server.url( "/boom" ) ).has_value() );
	REQUIRE( waitUntil( [ & ] { return session->idle(); } ) );
	const auto lanes { harness.context->laneSnapshots() };
	REQUIRE( lanes.size() == 1 );
	CHECK( lanes.front().backed_off );
	CHECK( lanes.front().parser_limited );
	CHECK( lanes.front().parser_error_hits == 1 );
	CHECK( lanes.front().rate_limit_hits == 0 );
	CHECK( lanes.front().rate == ( RequestRate { 3, 1 } ) );
	CHECK( harness.observer.failures.size() == 1 );
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

TEST_CASE( "Domain secrets reach the script", "[downloader][session][secrets]" )
{
	Harness harness {};
	Json::Value credentials { Json::objectValue };
	credentials[ "token" ] = "hunter2";
	harness.secrets.values.emplace( "127.0.0.1", std::move( credentials ) );
	harness.server.route(
		"/auth",
		[]( const test::TestRequest& request )
		{
			const auto found { request.headers.find( "authorization" ) };
			return test::TestResponse { .body = found == request.headers.end() ? "none" : found->second };
		} );

	harness.parsers.write( "test.js", R"JS(
export async function auth(input, idhan) {
    const credentials = idhan.secrets();
    const token = credentials.token;
    if (credentials.absent !== undefined) throw new Error("absent fields must be undefined");
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
	std::string domain_option {};
	bool invalid {};

	SECTION( "No domain uses downloader tags" )
	{}
	SECTION( "Single-domain import selectors are rejected" )
	{
		domain_option = R"(tagDomain: "other tags",)";
		invalid = true;
	}

	harness.imports.stored_record = 42;
	harness.parsers.write(
		"test.js",
		std::format(
			R"JS(
export function post(input, idhan) {{
    idhan.queueImport({{{} request: {{url: new URL("/file.png", input.url).href}}, tags: ["test"]}});
}}
)JS",
			domain_option ) );
	harness.configure( R"([{"export":"post","path":"/post"}])" );
	const auto session { harness.session( harness.server.url( "/post" ) ) };
	REQUIRE( session->submit( harness.server.url( "/post" ) ).has_value() );
	folly::coro::blockingWait( session->wait() );

	if ( invalid )
	{
		REQUIRE( harness.observer.failures.size() == 1 );
		CHECK( harness.observer.failures.front().second.contains( "tagDomain is no longer supported" ) );
		CHECK( harness.imports.lookup_requests.empty() );
		return;
	}

	idhan::test::checkNoFailures( harness.observer );
	REQUIRE( harness.imports.lookup_requests.size() == 1 );
	REQUIRE( harness.imports.finished_requests.size() == 1 );
	const auto& options { harness.imports.finished_requests.front().options };
	CHECK_FALSE( options.isMember( "tagDomain" ) );
	REQUIRE( options[ "tags" ].isObject() );
	CHECK( options[ "tags" ].size() == 1 );
	CHECK( options[ "tags" ][ "downloader tags" ][ 0 ].asString() == "test" );
}

TEST_CASE( "A parser sends tags to multiple explicit domains", "[downloader][session][tag-domain]" )
{
	Harness harness {};
	harness.imports.stored_record = 42;
	harness.parsers.write( "test.js", R"JS(
export function post(input, idhan) {
    idhan.queueImport({
        request: {url: new URL("/file.png", input.url).href},
        tags: {pixiv: ["artist:example", "landscape"], curated: ["favorite"], "mydomain.com": ["shared"]},
    });
}
)JS" );
	harness.configure( R"([{"export":"post","path":"/post"}])" );
	const auto session { harness.session( harness.server.url( "/post" ) ) };
	REQUIRE( session->submit( harness.server.url( "/post" ) ).has_value() );
	folly::coro::blockingWait( session->wait() );
	idhan::test::checkNoFailures( harness.observer );
	REQUIRE( harness.imports.lookup_requests.size() == 1 );
	REQUIRE( harness.imports.finished_requests.size() == 1 );
	const auto& options { harness.imports.finished_requests.front().options };
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
    idhan.queueImport({{request: {{url: new URL("/file.png", input.url).href}}, tags: {}}});
}}
)JS",
			tags ) );
	harness.configure( R"([{"export":"post","path":"/post"}])" );
	const auto session { harness.session( harness.server.url( "/post" ) ) };
	REQUIRE( session->submit( harness.server.url( "/post" ) ).has_value() );
	folly::coro::blockingWait( session->wait() );
	REQUIRE( harness.observer.failures.size() == 1 );
	CHECK( harness.observer.failures.front().second.contains( "idhan.import tags" ) );
	CHECK( harness.imports.lookup_requests.empty() );
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
	std::optional< bool > force_import {};
	SECTION( "Unknown files download" )
	{}
	SECTION( "Known files reuse the existing record without HTTP" )
	{
		harness.imports.stored_record = 42;
	}
	SECTION( "A runtime force flag bypasses reuse" )
	{
		harness.imports.stored_record = 42;
		DownloaderConfig config {};
		config.force_download = true;
		force_context = true;
		harness.context.reset();
		harness.configure( R"([{"export":"page","path":"/page"}])", config );
	}
	SECTION( "Session settings can disable the context force default" )
	{
		harness.imports.stored_record = 42;
		DownloaderConfig config {};
		config.force_download = true;
		force_import = false;
		harness.context.reset();
		harness.configure( R"([{"export":"page","path":"/page"}])", config );
	}
	SECTION( "Session settings can force imports" )
	{
		force_import = true;
		force_context = true;
		harness.imports.stored_record = 42;
	}
	SECTION( "Lookup failures prevent dispatch and are reported" )
	{
		harness.imports.lookup_error = "lookup failed";
	}

	SECTION( "The session force flag bypasses reuse" )
	{
		force_session = true;
		harness.imports.stored_record = 42;
	}
	if ( force_session || force_context ) harness.imports.lookup_error = "Forced imports must skip the lookup";

	const auto session { harness.context->createSession(
		SessionOptions { .force_download = force_session,
			.force_import = force_import,
			.root_url = harness.server.url( "/file.png" ),
		                 .observer = &harness.observer } ) };
	REQUIRE( session->submit( harness.server.url( "/file.png" ) ).has_value() );
	folly::coro::blockingWait( session->wait() );
	CHECK( harness.observer.failures.empty() );
	if ( force_session || force_context )
	{
		CHECK( harness.imports.lookup_requests.empty() );
		REQUIRE( harness.imports.finished_requests.size() == 1 );
		CHECK( harness.imports.finished_requests.front().force_download );
		CHECK_FALSE( harness.imports.finished_requests.front().stored_record.has_value() );
		CHECK( requests == 1 );
		REQUIRE( harness.imports.stored.size() == 1 );
		CHECK( harness.observer.import_failures.empty() );
		return;
	}

	REQUIRE( harness.imports.lookup_requests.size() == 1 );
	if ( harness.imports.lookup_error )
	{
		CHECK( requests == 0 );
		CHECK( harness.observer.import_failures.size() == 1 );
		CHECK( harness.observer.imports.empty() );
		return;
	}

	CHECK( harness.observer.import_failures.empty() );
	REQUIRE( harness.observer.imports.size() == 1 );
	if ( harness.imports.stored_record )
	{
		CHECK( requests == 0 );
		CHECK( harness.imports.stored.empty() );
		CHECK( harness.observer.imports.front().record_id == 42 );
		REQUIRE( harness.imports.opened_requests.size() == 1 );
		REQUIRE( harness.imports.finished_requests.size() == 1 );
		CHECK( harness.imports.finished_requests.front().stored_record == 42 );
		CHECK( harness.imports.async_writes.load() == 0 );
	}
	else
	{
		CHECK( requests == 1 );
		REQUIRE( harness.imports.stored.size() == 1 );
		CHECK( harness.imports.stored.front().second == "IMAGE" );
	}
}

TEST_CASE(
	"Stored files finish through the import sink and report completion failures",
	"[downloader][session][direct]" )
{
	Harness harness {};
	harness.imports.stored_record = 42;
	harness.imports.finish_error = "Could not apply tags";
	harness.parsers.write( "test.js", "export function page() {}" );
	harness.configure( R"([{"export":"page","path":"/page"}])" );
	const auto session { harness.session( harness.server.url( "/file.png" ) ) };
	REQUIRE( session->submit( session->rootUrl() ).has_value() );
	folly::coro::blockingWait( session->wait() );
	CHECK( harness.server.requestCount() == 0 );
	REQUIRE( harness.imports.lookup_requests.size() == 1 );
	REQUIRE( harness.imports.opened_requests.size() == 1 );
	CHECK( harness.imports.opened_requests.front().stored_record == 42 );
	CHECK( harness.imports.finished_requests.empty() );
	CHECK( harness.imports.async_writes.load() == 0 );
	CHECK( harness.imports.aborted.load() == 1 );
	CHECK( harness.observer.imports.empty() );
	REQUIRE( harness.observer.import_failures.size() == 1 );
	CHECK( harness.observer.import_failures.front().second.contains( "Could not apply tags" ) );
}

TEST_CASE( "Parser imports treat POST URLs with bodies as direct files", "[downloader][session][direct]" )
{
	Harness harness {};
	bool forced {};
	SECTION( "A stored file skips the request" )
	{
		harness.imports.stored_record = 42;
	}
	SECTION( "An unknown file is downloaded" )
	{}
	SECTION( "Force skips the lookup even for a stored file" )
	{
		forced = true;
		harness.imports.stored_record = 42;
		harness.imports.lookup_error = "Forced imports must skip the lookup";
	}

	harness.server.route(
		"/file",
		[]( const test::TestRequest& )
		{ return test::TestResponse { .headers = { { "Content-Type", "image/png" } }, .body = "IMAGE" }; } );
	harness.parsers.write( "test.js", R"JS(
export async function page(input, idhan) {
    await idhan.queueImport({
        request: {url: new URL("/file", input.url).href, method: "POST", body: "file=example"},
        tags: ["direct file"], forceDownload: input.url.includes("force")
    }).importFinished;
}
)JS" );
	harness.configure( R"([{"export":"page","path":"/page"}])" );
	const auto session { harness.session( harness.server.url( forced ? "/page?force=1" : "/page" ) ) };
	REQUIRE( session->submit( session->rootUrl() ).has_value() );
	folly::coro::blockingWait( session->wait() );
	test::checkNoFailures( harness.observer );
	CHECK( harness.observer.import_failures.empty() );
	REQUIRE( harness.observer.imports.size() == 1 );
	REQUIRE( harness.imports.finished_requests.size() == 1 );
	const auto& finished { harness.imports.finished_requests.front() };
	CHECK( finished.options[ "tags" ][ "downloader tags" ][ 0 ].asString() == "direct file" );
	CHECK( harness.imports.lookup_requests.size() == ( forced ? 0 : 1 ) );
	if ( harness.imports.stored_record.has_value() && !forced )
	{
		CHECK( harness.server.requestCount() == 0 );
		CHECK( harness.imports.async_writes.load() == 0 );
		CHECK( finished.stored_record == 42 );
		CHECK( harness.observer.imports.front().record_id == 42 );
		return;
	}

	const auto requests { harness.server.requests() };
	REQUIRE( requests.size() == 1 );
	CHECK( requests.front().method == "POST" );
	CHECK( requests.front().body == "file=example" );
	CHECK_FALSE( finished.stored_record.has_value() );
	CHECK( finished.force_download == forced );
	REQUIRE( harness.imports.stored.size() == 1 );
	CHECK( harness.imports.stored.front().second == "IMAGE" );
}

TEST_CASE(
	"Follow routes an unknown source directly to import and matching pages to their parser",
	"[downloader][session][follow-routing]" )
{
	Harness harness {};
	harness.parsers.write( "test.js", R"JS(
export async function gallery(input, idhan) {
    const source = new URL("/myrandomimage.jpg", input.url);
    source.hostname = "localhost";
    await idhan.follow({url: source.href, tags: {gelbooru: ["creator:example"]}});
    await idhan.follow({url: source.href});
    await idhan.follow({url: new URL("/known", input.url).href});
}
export async function known(input, idhan) {
    await idhan.queueImport({request: {url: new URL("/known-file", input.url).href}}).importFinished;
}
)JS" );
	harness.parsers.write( "probe.js", R"JS(
export default class Probe {
    static metadata = {formatVersion: 1, sdkVersion: '1.1.0', id: 'probe', version: 1,
        options: [], handlers: [], discoveries: [{method: 'discover', urlClass: 'probe'}], urlClasses: []};
}
)JS" );
	std::filesystem::create_directories( harness.parsers.root() / "packages/sdk" );
	harness.parsers.write( "packages/sdk/discovery.js", R"JS(
export default class Discovery {
    static metadata = {formatVersion: 1, sdkVersion: '1.1.0', id: 'discovery', version: 1,
        options: [], handlers: [], discoveries: [], urlClasses: []};
}
export function run() { throw new Error("Followed files must not run discovery"); }
)JS" );
	harness.configure( R"([{"export":"gallery","path":"/gallery"},{"export":"known","path":"/known"}])" );
	harness.server.route(
		"/myrandomimage.jpg",
		[]( const test::TestRequest& )
		{ return test::TestResponse { .headers = { { "Content-Type", "image/jpeg" } }, .body = "SOURCE" }; } );
	harness.server.route(
		"/known-file",
		[]( const test::TestRequest& )
		{ return test::TestResponse { .headers = { { "Content-Type", "image/jpeg" } }, .body = "KNOWN" }; } );
	const auto session { harness.session( harness.server.url( "/gallery" ) ) };
	REQUIRE( session->submit( session->rootUrl() ).has_value() );
	folly::coro::blockingWait( session->wait() );
	test::checkNoFailures( harness.observer );
	CHECK( harness.observer.import_failures.empty() );
	CHECK( harness.server.requestCount() == 2 );
	CHECK( harness.observer.started.size() == 3 );
	REQUIRE( harness.imports.finished_requests.size() == 2 );
	const auto source = std::ranges::find_if(
		harness.imports.finished_requests,
		[]( const ImportRequest& request ) { return request.url.contains( "localhost" ); } );
	REQUIRE( source != harness.imports.finished_requests.end() );
	CHECK( source->options[ "tags" ][ "gelbooru" ][ 0 ].asString() == "creator:example" );
	REQUIRE( harness.imports.stored.size() == 2 );
	CHECK(
		std::ranges::any_of( harness.imports.stored, []( const auto& record ) { return record.second == "SOURCE"; } ) );
	CHECK(
		std::ranges::any_of( harness.imports.stored, []( const auto& record ) { return record.second == "KNOWN"; } ) );
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
	folly::coro::blockingWait( session->wait() );
	CHECK( harness.imports.stored.empty() );
	REQUIRE( harness.observer.import_failures.size() == 1 );
	CHECK( harness.observer.import_failures.front().second.contains( "single file" ) );
	const auto lanes { harness.context->laneSnapshots() };
	REQUIRE( lanes.size() == 1 );
	CHECK_FALSE( lanes.front().backed_off );

	harness.server.route(
		"/file.png",
		[]( const test::TestRequest& )
		{ return test::TestResponse { .headers = { { "Content-Type", "image/png" } }, .body = "IMAGE" }; } );
	REQUIRE( session->submit( harness.server.url( "/file.png" ) ).has_value() );
	REQUIRE( waitUntil( [ & ] { return session->idle(); }, std::chrono::seconds { 3 } ) );
	REQUIRE( harness.imports.stored.size() == 1 );
	CHECK( harness.imports.stored.front().second == "IMAGE" );
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
    idhan.queueImport({request: {url: new URL("/download", input.url).href},
        tags: ["test"], forceDownload: true});
}
)JS" );
	harness.configure( R"([{"export":"page","path":"/page"}])" );
	harness.imports.stored_record = 42;
	SECTION( "Force download" )
	{}
	SECTION( "Reuse carries metadata" )
	{
		harness.parsers.write( "test.js", R"JS(
export function page(input, idhan) {
    idhan.queueImport({request: {url: new URL("/download", input.url).href},
        tags: ["test"], urls: [{url: input.url, type: "post"}]});
}
)JS" );
	}
	const auto session { harness.session( harness.server.url( "/page" ) ) };
	REQUIRE( session->submit( harness.server.url( "/page" ) ).has_value() );
	folly::coro::blockingWait( session->wait() );
	CHECK( harness.observer.import_failures.empty() );
	REQUIRE( harness.observer.imports.size() == 1 );
	REQUIRE( harness.imports.finished_requests.size() == 1 );
	CHECK( harness.imports.finished_requests.front().options[ "tags" ][ "downloader tags" ][ 0 ].asString() == "test" );
	if ( harness.imports.lookup_requests.empty() )
	{
		CHECK( requests == 1 );
		CHECK( harness.imports.stored.size() == 1 );
	}
	else
	{
		CHECK( requests == 0 );
		CHECK( harness.observer.imports.front().record_id == 42 );
		CHECK(
			harness.imports.finished_requests.front().options[ "tags" ][ "downloader tags" ][ 0 ].asString()
		    == "test" );
		CHECK(
			harness.imports.finished_requests.front().options[ "urls" ][ 0 ][ "url" ].asString()
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
    idhan.queueImport({request: {url: new URL("/file", input.url).href}, urls: [
        {url: post, type: "post"},
        {url: new URL("/file", input.url).href, type: "file"},
        {url: new URL("/source", input.url).href, type: "unknown"},
    ]});
}
export function post() {}
)JS" );
	harness.configure( R"([{"export":"gallery","path":"/gallery"},{"export":"post","path":"/post"}])" );
	harness.imports.stored_record = 42;
	const auto session { harness.session( harness.server.url( "/gallery" ) ) };
	REQUIRE( session->submit( harness.server.url( "/gallery" ) ).has_value() );
	folly::coro::blockingWait( session->wait() );
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
		call = R"(idhan.queueImport({request: {url: input.url}, urls: [
            {url: input.url, type: "post"}, {url: input.url, type: "invalid"}] }))";
	}
	SECTION( "Nonarray import URLs" )
	{
		call = R"(idhan.queueImport({request: {url: input.url}, urls: "post"}))";
	}
	SECTION( "Invalid import declaration" )
	{
		call = R"(idhan.queueImport({request: {url: input.url}, urls: ["post"]}))";
	}
	harness.parsers.write( "test.js", "export function gallery(input, idhan) { " + call + "; }" );
	harness.configure( R"([{"export":"gallery","path":"/gallery"}])" );
	const auto session { harness.session( harness.server.url( "/gallery" ) ) };
	REQUIRE( session->submit( harness.server.url( "/gallery" ) ).has_value() );
	folly::coro::blockingWait( session->wait() );
	CHECK( harness.observer.failures.size() == 1 );
	CHECK( harness.observer.started.size() == 1 );
	CHECK( harness.observer.url_types.empty() );
	CHECK( harness.observer.follows.empty() );
	CHECK( harness.observer.imports.empty() );
	CHECK( harness.imports.lookup_requests.empty() );
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
		call = R"(idhan.queueImport({request: {url: input.url}, urls: [{url: input.url, type: "post"}]}))";
	}
	harness.observer.url_type_error = "URL classification database unavailable";
	harness.parsers.write( "test.js", "export function gallery(input, idhan) { " + call + "; }" );
	harness.configure( R"([{"export":"gallery","path":"/gallery"}])" );
	const auto session { harness.session( harness.server.url( "/gallery" ) ) };
	REQUIRE( session->submit( harness.server.url( "/gallery" ) ).has_value() );
	folly::coro::blockingWait( session->wait() );
	REQUIRE( harness.observer.failures.size() == 1 );
	CHECK( harness.observer.failures.front().second.contains( "URL classification database unavailable" ) );
	CHECK( harness.observer.url_types.empty() );
	CHECK( harness.observer.follows.empty() );
	CHECK( harness.imports.lookup_requests.empty() );
}

TEST_CASE(
	"Follow comparisons normalize queries while requests retain signatures",
	"[downloader][session][url-normalization]" )
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
	folly::coro::blockingWait( session->wait() );
	test::checkNoFailures( harness.observer );
	CHECK( harness.observer.import_failures.empty() );
	CHECK( harness.server.requestCount() == 2 );
	REQUIRE( harness.observer.follows.size() == 4 );
	CHECK( harness.observer.follows[ 0 ].second == FollowStatus::QUEUED );
	CHECK( harness.observer.follows[ 1 ].second == FollowStatus::ALREADY_EXPLORED );
	CHECK( harness.observer.follows[ 2 ].second == FollowStatus::QUEUED );
	CHECK( harness.observer.follows[ 3 ].second == FollowStatus::ALREADY_EXPLORED );
	REQUIRE( harness.imports.lookup_requests.size() == 2 );
	for ( const auto& request : harness.imports.lookup_requests )
	{
		CHECK_FALSE( request.url.contains( "ex=" ) );
		CHECK_FALSE( request.url.contains( "hm=" ) );
	}
	for ( const auto& request : harness.server.requests() )
	{
		CHECK( request.target.contains( "ex=" ) );
		CHECK( request.target.contains( "hm=" ) );
	}
}

TEST_CASE(
	"Normalization applies to imports and declarations without changing HTTP",
	"[downloader][session][url-normalization]" )
{
	Harness harness {};
	harness.parsers.write( "test.js", R"JS(
export function page(input, idhan) {
    idhan.setUrlType({url: new URL("/post?z=2&a=1", input.url).href, type:"post"});
    idhan.follow({url: new URL("/child?z=2&a=1", input.url).href});
    idhan.queueImport({request:{url: new URL("/file?z=2&ex=sig%2braw&a=1&hm=hash", input.url).href},
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
		harness.imports.stored_record = 42;
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
	folly::coro::blockingWait( session->wait() );
	test::checkNoFailures( harness.observer );
	CHECK( harness.observer.import_failures.empty() );
	REQUIRE( harness.imports.lookup_requests.size() == 1 );
	const auto& stored { harness.imports.lookup_requests.front() };
	CHECK(
		stored.url
	    == harness.server.url( enabled ? "/file?a=1&z=2" : "/file?z=2&a=1" ) );
	CHECK( stored.options[ "urls" ][ 0 ][ "url" ].asString() == "https://nosort.example/post?z=2&a=1" );
	CHECK( stored.options[ "discoveredUrls" ][ 0 ].asString() == "https://sort.example/post?a=1&z=2" );
	if ( reused )
	{
		REQUIRE( harness.imports.metadata.size() == 1 );
		REQUIRE( harness.imports.finished_requests.size() == 1 );
		CHECK( harness.imports.finished_requests.front().stored_record == 42 );
		CHECK( harness.imports.metadata.front().effective_url.empty() );
		CHECK( harness.server.requestCount() == 0 );
		return;
	}

	REQUIRE( harness.imports.metadata.size() == 1 );
	CHECK(
		harness.imports.metadata.front().effective_url
	    == harness.server.url( enabled ? "/final?a=1&z=2" : "/final?z=2&a=1" ) );
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

TEST_CASE( "Session parser overrides preserve defaults and stay isolated", "[downloader][session][options]" )
{
	Harness harness {};
	harness.parsers.write( "test.js", R"JS(
export function page(input) {
    const expected = new URL(input.url).searchParams.get("value");
    if (input.options.value !== expected || input.options.retained !== "default")
        throw new Error("Session options leaked or discarded context defaults");
}
)JS" );
	DownloaderConfig config {};
	config.options[ "test" ][ "value" ] = "context";
	config.options[ "test" ][ "retained" ] = "default";
	harness.configure( R"([{"export":"page","path":"/page"}])", config );

	Json::Value overrides { Json::objectValue };
	overrides[ "test" ][ "value" ] = "session";
	const auto customized {
		harness.context->createSession( SessionOptions { .parser_options = overrides, .observer = &harness.observer } )
	};
	const auto ordinary { harness.session( "" ) };
	REQUIRE( customized->submit( harness.server.url( "/page?value=session" ) ).has_value() );
	REQUIRE( ordinary->submit( harness.server.url( "/page?value=context" ) ).has_value() );
	folly::coro::blockingWait( customized->wait() );
	folly::coro::blockingWait( ordinary->wait() );
	idhan::test::checkNoFailures( harness.observer );
}

TEST_CASE( "Follow tags reach descendant imports alongside parser tags", "[downloader][follow-tags]" )
{
	Harness harness;
	harness.parsers.write( "test.js", R"JS(
export async function gallery(input) {
    await idhan.follow({url: new URL('/post', input.url).href, tags: {gelbooru: ['creator:artist']}});
}
export async function post(input) {
    await idhan.follow({url: new URL('/image', input.url).href});
}
export function image(input) {
    idhan.queueImport({request: {url: new URL('/file', input.url).href}, tags: {pixiv: ['original']}});
}
)JS" );
	harness.configure(
		R"([{"export":"gallery","path":"/gallery"},{"export":"post","path":"/post"},{"export":"image","path":"/image"}])" );
	harness.imports.stored_record = 42;
	const auto session { harness.session( harness.server.url( "/gallery" ) ) };
	REQUIRE( session->submit( harness.server.url( "/gallery" ) ).has_value() );
	folly::coro::blockingWait( session->wait() );
	idhan::test::checkNoFailures( harness.observer );
	REQUIRE( harness.imports.lookup_requests.size() == 1 );
	const auto& tags { harness.imports.lookup_requests.front().options[ "tags" ] };
	CHECK( tags[ "gelbooru" ][ 0 ].asString() == "creator:artist" );
	CHECK( tags[ "pixiv" ][ 0 ].asString() == "original" );
}

TEST_CASE( "Session waits count queued parsers separately from HTTP", "[downloader][session][network-stats]" )
{
	Harness harness {};
	harness.parsers.write( "test.js", R"JS(
export function post() {
    const end = Date.now() + 500;
    while (Date.now() < end) {}
}
)JS" );
	harness.configure( R"([{"export":"post","path":"/post"}])" );
	const auto blocker { harness.session( "" ) };
	REQUIRE( blocker->submit( harness.server.url( "/post?blocker=1" ) ).has_value() );
	REQUIRE( waitUntil( [ & ] { return blocker->snapshot().script_executing; } ) );
	const auto session { blocker };
	REQUIRE( session->submit( harness.server.url( "/post?i=1" ) ).has_value() );
	REQUIRE( session->submit( harness.server.url( "/post?i=2" ) ).has_value() );
	const bool queued { waitUntil(
		[ & ]
		{
			const auto snapshot { session->snapshot() };
			return snapshot.script_executing && snapshot.waiting_parsers == 2;
		} ) };
	const auto snapshot { session->snapshot() };
	folly::coro::blockingWait( session->wait() );

	REQUIRE( queued );
	CHECK( snapshot.downloading == 0 );
	CHECK( snapshot.waiting_http_rate == 0 );
	CHECK( snapshot.waiting_http_shard == 0 );
	CHECK( session->snapshot().waiting_parsers == 0 );
	idhan::test::checkNoFailures( harness.observer );
}

TEST_CASE(
	"Session download totals and average import size use received bytes",
	"[downloader][session][network-stats]" )
{
	Harness harness {};
	harness.server.route(
		"/first", []( const test::TestRequest& ) { return test::TestResponse { .body = std::string( 100, 'a' ) }; } );
	harness.server.route(
		"/second", []( const test::TestRequest& ) { return test::TestResponse { .body = std::string( 300, 'b' ) }; } );
	harness.parsers.write( "test.js", R"JS(
export async function post(input, idhan) {
    await idhan.queueImport({request: {url: new URL("/first", input.url).href}}).importFinished;
    await idhan.queueImport({request: {url: new URL("/second", input.url).href}}).importFinished;
}
)JS" );
	harness.configure( R"([{"export":"post","path":"/post"}])" );
	const auto session { harness.session( "" ) };
	REQUIRE( session->submit( harness.server.url( "/post" ) ).has_value() );
	folly::coro::blockingWait( session->wait() );
	idhan::test::checkNoFailures( harness.observer );
	const auto snapshot { session->snapshot() };
	REQUIRE( snapshot.counters.imported == 2 );
	CHECK( snapshot.downloaded_bytes == 400 );
	CHECK( snapshot.average_import_bytes == 200 );
}

TEST_CASE( "Awaited file imports report whether the record was already stored", "[downloader][session][import-result]" )
{
	Harness harness;
	bool existing {};
	SECTION( "A downloaded file is new" )
	{}
	SECTION( "A stored URL reuses its record" )
	{
		existing = true;
		harness.imports.stored_record = 42;
	}

	harness.server.route(
		"/file.png",
		[]( const test::TestRequest& )
		{ return test::TestResponse { .headers = { { "Content-Type", "image/png" } }, .body = "IMAGE" }; } );
	harness.parsers.write(
		"test.js",
		std::format(
			R"JS(
export async function page(input, host) {{
    const file = new URL('/file.png', input.url).href;
    const handle = host.queueImport({{request: {{url: file, referer: input.url}}, filename: 'file.png'}});
    const status = handle.importFinished;
    if (status !== handle.importFinished) throw new Error('importFinished must reuse its promise');
    const result = await status;
    if (result.existing !== {}) throw new Error('Incorrect existing-file result');
}}
)JS",
			existing ? "true" : "false" ) );
	harness.configure( R"([{"export":"page","path":"/page"}])" );
	const auto session { harness.session( harness.server.url( "/page" ) ) };
	REQUIRE( session->submit( harness.server.url( "/page" ) ).has_value() );
	folly::coro::blockingWait( session->wait() );
	idhan::test::checkNoFailures( harness.observer );
	REQUIRE( harness.imports.finished_requests.size() == 1 );
}

TEST_CASE(
	"Stored direct imports bypass a busy lane without consuming rate allowance",
	"[downloader][session][direct][reuse-rate]" )
{
	using namespace std::chrono_literals;
	Harness harness;
	bool from_parser {};
	SECTION( "Direct file URL" )
	{}
	SECTION( "Parser queueImport" )
	{
		from_parser = true;
	}

	harness.server.route(
		"/prime.png",
		[]( const test::TestRequest& )
		{ return test::TestResponse { .headers = { { "Content-Type", "image/png" } }, .body = "IMAGE" }; } );
	harness.server.route(
		"/next.png",
		[]( const test::TestRequest& )
		{ return test::TestResponse { .headers = { { "Content-Type", "image/png" } }, .body = "IMAGE" }; } );
	harness.parsers.write( "test.js", R"JS(
export function page(input, idhan) {
    idhan.queueImport({request: {url: new URL("/stored.png", input.url).href}});
}
)JS" );
	harness.configure( R"([{"export":"page","path":"/page"}])", {}, R"({"requests":1,"seconds":60})" );
	const auto prime_url { harness.server.url( "/prime.png" ) };
	const auto prime { harness.session( prime_url ) };
	REQUIRE( prime->submit( prime_url ).has_value() );
	REQUIRE( waitUntil( [ & ] { return prime->idle(); }, 3s ) );
	REQUIRE( harness.observer.import_failures.empty() );
	REQUIRE( harness.server.requestCount() == 1 );
	const auto before { harness.context->laneSnapshots() };
	REQUIRE( before.size() == 1 );
	REQUIRE( before.front().dispatched == 1 );
	REQUIRE( before.front().remaining > 50s );

	harness.imports.stored_record = 42;
	const auto stored_url { harness.server.url( from_parser ? "/page" : "/stored.png" ) };
	const auto stored { harness.session( stored_url ) };
	REQUIRE( stored->submit( stored_url ).has_value() );
	REQUIRE( waitUntil( [ & ] { return stored->idle(); }, 3s ) );
	REQUIRE( harness.observer.failures.empty() );
	REQUIRE( harness.observer.import_failures.empty() );
	REQUIRE( harness.observer.imports.size() == 2 );
	REQUIRE( harness.observer.imports.back().record_id == 42 );
	REQUIRE( harness.server.requestCount() == 1 );
	const auto after { harness.context->laneSnapshots() };
	REQUIRE( after.size() == 1 );
	REQUIRE( after.front().dispatched == before.front().dispatched );
	REQUIRE( after.front().queued == 0 );
	REQUIRE( after.front().remaining > 50s );
	REQUIRE( after.front().remaining <= before.front().remaining );

	// A real download still waits for the untouched allowance; only an explicit reset releases it.
	harness.imports.stored_record.reset();
	const auto next_url { harness.server.url( "/next.png" ) };
	const auto next { harness.session( next_url ) };
	REQUIRE( next->submit( next_url ).has_value() );
	REQUIRE( waitUntil(
		[ & ]
		{
			const auto lanes { harness.context->laneSnapshots() };
			return lanes.size() == 1 && lanes.front().queued == 1;
		},
		3s ) );
	REQUIRE( harness.server.requestCount() == 1 );
	harness.context->resetAllBackoff();
	REQUIRE( waitUntil( [ & ] { return next->idle(); }, 3s ) );
	REQUIRE( harness.server.requestCount() == 2 );
	REQUIRE( harness.observer.import_failures.empty() );
}
