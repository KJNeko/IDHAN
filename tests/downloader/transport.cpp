#include <folly/coro/BlockingWait.h>
#include "http/LanePool.hpp"
#include <folly/coro/Baton.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <future>
#include <string>
#include <thread>
#include <vector>

#include "IDHANDownloader/http/Bandwidth.hpp"

// Folly/glog also defines CHECK; load Catch assertions last.
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "Harness.hpp"

using namespace idhan;
using namespace idhan::downloader;
using idhan::test::Harness;
using idhan::test::waitUntil;

constexpr std::string_view passthrough_script { R"JS(
export async function fetchIt(input, idhan) {
    const response = await idhan.request({url: input.url});
    globalThis.__lastStatus = response.status;
}
)JS" };

static void runOnce( Harness& harness, const std::string& url )
{
	const auto session { harness.session( url ) };
	REQUIRE( session->submit( url ).has_value() );
	REQUIRE( waitUntil( [ & ] { return session->idle(); } ) );
}

TEST_CASE(
	"Rate limited lanes overlap slow requests without exceeding the start rate",
	"[downloader][lane-concurrency]" )
{
	Harness harness {};
	std::atomic_bool release {};
	std::mutex mutex {};
	std::vector< std::chrono::steady_clock::time_point > arrivals;
	harness.server.route(
		"/page",
		[ & ]( const test::TestRequest& )
		{
			{
				const std::scoped_lock lock { mutex };
				arrivals.push_back( std::chrono::steady_clock::now() );
			}
			while ( !release.load() ) std::this_thread::sleep_for( std::chrono::milliseconds { 2 } );
			return test::TestResponse { .body = "ok" };
		} );
	harness.parsers.write( "test.js", passthrough_script );
	DownloaderConfig config {};
	config.io_threads = 1;
	harness.configure(
		R"([{"export":"fetchIt","path":"/page"}])", config, R"({"requests":3,"seconds":1,"concurrency":6})" );
	const auto session { harness.session( "" ) };
	for ( int index {}; index < 6; ++index )
		REQUIRE( session->submit( harness.server.url( "/page?i=" + std::to_string( index ) ) ).has_value() );

	const bool overlapped { waitUntil(
		[ & ]
		{
			const std::scoped_lock lock { mutex };
			return arrivals.size() == 6;
		},
		std::chrono::seconds { 3 } ) };
	const auto lanes { harness.context->laneSnapshots() };
	const auto network { session->snapshot() };
	const auto reported { harness.lane_observer.snapshots() };
	release.store( true );
	folly::coro::blockingWait( session->wait() );
	idhan::test::checkNoFailures( harness.observer );
	REQUIRE( overlapped );
	CHECK( network.downloading == 6 );
	CHECK( network.waiting_requests == 6 );
	CHECK( network.waiting == 0 );
	CHECK( network.waiting_http_rate == 0 );
	CHECK( network.waiting_http_shard == 0 );
	CHECK( network.waiting_storage == 0 );
	REQUIRE( lanes.size() == 1 );
	CHECK( lanes.front().in_flight == 6 );
	CHECK( lanes.front().concurrency_limit == 6 );
	CHECK( lanes.front().queued == 0 );

	CHECK( std::ranges::any_of( reported, []( const LaneSnapshot& lane ) { return lane.in_flight == 6; } ) );
	const std::scoped_lock lock { mutex };
	REQUIRE( arrivals.size() == 6 );
	CHECK( arrivals[ 2 ] - arrivals[ 0 ] < std::chrono::milliseconds { 500 } );
	CHECK( arrivals[ 3 ] - arrivals[ 0 ] >= std::chrono::milliseconds { 900 } );
}

TEST_CASE( "Request rate concurrency limits hold when the rate gate is open", "[downloader][lane-concurrency]" )
{
	Harness harness {};
	std::atomic_bool release {};
	harness.server.route(
		"/page",
		[ & ]( const test::TestRequest& )
		{
			while ( !release.load() ) std::this_thread::sleep_for( std::chrono::milliseconds { 2 } );
			return test::TestResponse { .body = "ok" };
		} );
	harness.parsers.write( "test.js", passthrough_script );
	std::size_t ceiling { 1 };
	std::string rate { R"({"requests":100,"seconds":1})" };
	std::string lane { "{}" };
	SECTION( "Default permits one request" )
	{}
	SECTION( "Request rate permits two requests" )
	{
		ceiling = 2;
		rate = R"({"requests":100,"seconds":1,"concurrency":2})";
	}
	SECTION( "Existing lane override takes precedence" )
	{
		rate = R"({"requests":100,"seconds":1,"concurrency":2})";
		lane = R"({"concurrency":1})";
	}
	harness.configure( R"([{"export":"fetchIt","path":"/page"}])", DownloaderConfig { .io_threads = 1 }, rate, lane );
	const auto session { harness.session( "" ) };
	for ( int index {}; index < 3; ++index )
		REQUIRE( session->submit( harness.server.url( "/page?i=" + std::to_string( index ) ) ).has_value() );

	const bool capped { waitUntil(
		[ & ]
		{
			const auto lanes { harness.context->laneSnapshots() };
			return harness.server.requestCount() == ceiling && lanes.size() == 1 && lanes.front().queued == 3 - ceiling
		        && lanes.front().remaining == std::chrono::steady_clock::duration::zero();
		} ) };
	const auto lanes { harness.context->laneSnapshots() };
	const auto network { session->snapshot() };
	const auto other { harness.session( "" )->snapshot() };
	release.store( true );
	folly::coro::blockingWait( session->wait() );
	idhan::test::checkNoFailures( harness.observer );
	REQUIRE( capped );
	CHECK( network.downloading == ceiling );
	CHECK( network.waiting_requests == 3 );
	CHECK( network.waiting == 3 - ceiling );
	CHECK( network.waiting_http_shard == 3 - ceiling );
	CHECK( network.waiting_http_rate == 0 );
	CHECK( network.waiting_storage == 0 );
	CHECK( other.downloading == 0 );
	CHECK( other.waiting == 0 );
	CHECK( lanes.front().in_flight == ceiling );
	CHECK( lanes.front().concurrency_limit == ceiling );
	CHECK( lanes.front().dispatch_wait == "concurrency" );
}

namespace idhan::test
{
class DelayedLaneSink final : public ImportSink
{
	folly::coro::Baton& m_release;
	std::atomic_bool& m_finishing;

  public:

	DelayedLaneSink( folly::coro::Baton& release, std::atomic_bool& finishing ) :
	  m_release( release ),
	  m_finishing( finishing )
	{}

	folly::coro::Task< std::expected< void, std::string > > write( std::span< const std::byte > ) override
	{
		co_return std::expected< void, std::string > {};
	}

	folly::coro::Task< std::expected< ImportResult, std::string > > finish( ImportMetadata ) override
	{
		m_finishing = true;
		co_await m_release;
		co_return ImportResult { .record_id = 1 };
	}

	void abort() override {}
};
} // namespace idhan::test

TEST_CASE( "Lane in-flight slots remain occupied until import completion", "[downloader][lane-concurrency]" )
{
	test::TestServer server;
	server.route(
		"/file",
		[]( const test::TestRequest& )
		{ return test::TestResponse { .headers = { { "Content-Type", "image/png" } }, .body = "PNG" }; } );
	test::RecordingLaneObserver observer;
	LanePool::Config config {};
	config.http_version = HttpVersion::HTTP_1_1;
	config.observer = &observer;
	LanePool pool {
		config, []( std::string_view ) { return LaneSettings { .rate = RequestRate { 100, 1 }, .concurrency = 1 }; }, 1
	};
	folly::coro::Baton release;
	std::atomic_bool finishing {};
	std::atomic_size_t completed {};
	std::atomic_size_t failures {};
	TransferRequest first {};
	first.url = server.url( "/file" );
	first.sink = std::make_unique< test::DelayedLaneSink >( release, finishing );
	const auto completion = [ & ]( TransferResult result )
	{
		if ( !result.has_value() ) ++failures;

		++completed;
	};
	pool.send( std::move( first ), completion );
	TransferRequest second {};
	second.url = server.url( "/file" );
	pool.send( std::move( second ), completion );
	const bool finalizing { waitUntil(
		[ & ]
		{
			const auto lanes { pool.snapshots() };
			return finishing.load() && lanes.size() == 1 && lanes.front().in_flight == 1 && lanes.front().queued == 1;
		} ) };
	const auto lanes { pool.snapshots() };
	const auto published { observer.snapshots() };
	release.post();
	const bool drained { waitUntil( [ & ] { return completed.load() == 2; } ) };
	folly::coro::blockingWait( pool.shutdown() );
	REQUIRE( finalizing );
	CHECK( lanes.front().in_flight == 1 );
	CHECK( lanes.front().dispatch_wait == "concurrency" );
	CHECK(
		std::ranges::any_of(
			published, []( const LaneSnapshot& lane ) { return lane.in_flight == 1 && lane.queued == 1; } ) );
	CHECK( drained );
	CHECK( failures.load() == 0 );
}

TEST_CASE(
	"Queued imports run before ordinary requests in submission order",
	"[downloader][transport][import-priority]" )
{
	test::TestServer server;
	for ( const auto* path : { "/first", "/request-1", "/import-1", "/request-2", "/import-2" } )
		server.route(
			path,
			[]( const test::TestRequest& )
			{ return test::TestResponse { .headers = { { "Content-Type", "image/png" } }, .body = "PNG" }; } );

	test::MemoryImportFactory imports;
	LanePool::Config config {};
	config.http_version = HttpVersion::HTTP_1_1;
	LanePool pool { config, []( std::string_view ) { return LaneSettings { .rate = RequestRate { 100, 1 } }; }, 1 };
	folly::coro::Baton release;
	std::atomic_bool finishing {};
	std::atomic_size_t completed {};
	std::atomic_size_t cancelled {};
	std::atomic_size_t failures {};
	const auto cancellation { std::make_shared< std::atomic_bool >( false ) };
	const auto completion = [ & ]( TransferResult result )
	{
		if ( !result.has_value() )
		{
			if ( result.error().code == TransferErrorCode::CANCELLED )
				++cancelled;
			else
				++failures;
		}

		++completed;
	};
	TransferRequest first {};
	first.url = server.url( "/first" );
	first.sink = std::make_unique< test::DelayedLaneSink >( release, finishing );
	pool.send( std::move( first ), completion );
	const bool held { waitUntil( [ & ] { return finishing.load(); } ) };
	for ( const auto* path : { "/request-1", "/import-1", "/request-2", "/import-2" } )
	{
		TransferRequest request {};
		request.url = server.url( path );
		const std::string_view name { path };
		if ( name.starts_with( "/import" ) )
		{
			request.import.url = request.url;
			auto sink_e { imports.open( request.import ) };
			REQUIRE( sink_e.has_value() );
			request.sink = std::move( sink_e.value() );
		}

		if ( name.ends_with( "2" ) ) request.cancellation = cancellation;

		pool.send( std::move( request ), completion );
	}
	const auto queued { pool.snapshots() };
	bool cancel {};
	SECTION( "Drain both queues" )
	{}
	SECTION( "Cancel work in both queues" )
	{
		cancel = true;
		pool.cancel( cancellation );
	}
	release.post();
	const bool drained { waitUntil( [ & ] { return completed.load() == 5; } ) };
	folly::coro::blockingWait( pool.shutdown() );
	REQUIRE( held );
	REQUIRE( queued.size() == 1 );
	CHECK( queued.front().queued == 4 );
	CHECK( drained );
	CHECK( failures.load() == 0 );
	CHECK( cancelled.load() == ( cancel ? 2 : 0 ) );
	std::vector< std::string > order;
	for ( const auto& request : server.requests() ) order.push_back( request.path );

	if ( cancel )
		CHECK( order == std::vector< std::string > { "/first", "/import-1", "/request-1" } );
	else
		CHECK( order == std::vector< std::string > { "/first", "/import-1", "/import-2", "/request-1", "/request-2" } );
}

TEST_CASE( "A redirect chain is followed hop by hop", "[downloader][transport][bandwidth]" )
{
	(void)takeBandwidthLogs();
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
	const auto logs { takeBandwidthLogs() };
	REQUIRE( logs.size() == 3 );
	for ( const auto& log : logs )
	{
		CHECK( log.host == "127.0.0.1" );
		CHECK( log.timestamp > 0 );
		CHECK( log.bytes_received > 0 );
	}
	CHECK( takeBandwidthLogs().empty() );
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

TEST_CASE( "A request reports when it was made, queued, sent and received", "[downloader][transport][timing]" )
{
	Harness harness {};
	harness.server.route( "/page", []( const test::TestRequest& ) { return test::TestResponse { .body = "ok" }; } );
	harness.parsers.write( "test.js", passthrough_script );
	harness.configure( R"([{"export":"fetchIt","path":"/page"}])" );

	runOnce( harness, harness.server.url( "/page" ) );

	idhan::test::checkNoFailures( harness.observer );
	REQUIRE( harness.observer.requests.size() == 1 );
	const RequestTiming& timing { harness.observer.requests.front().timing };
	CHECK( timing.attempts == 1 );
	REQUIRE( timing.queued.has_value() );
	REQUIRE( timing.sent.has_value() );
	REQUIRE( timing.sent_wall.has_value() );
	REQUIRE( timing.received.has_value() );
	CHECK( timing.made <= timing.queued.value() );
	CHECK( timing.queued.value() <= timing.sent.value() );
	CHECK( timing.sent.value() <= timing.received.value() );
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
	CHECK( lane.rate.requests == 1 );
	CHECK( lane.rate.seconds == 5 );

	REQUIRE( waitUntil(
		[ & ]
		{
			const auto current { harness.context->laneSnapshots() };
			return !current.empty() && current.front().download_bytes_per_second > 0;
		} ) );
	REQUIRE( waitUntil(
		[ & ]
		{
			const auto current { harness.lane_observer.snapshots() };
			return !current.empty() && current.back().download_bytes_per_second == 0 && current.back().in_flight == 0;
		} ) );
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
		DownloaderConfig { .lane_error_backoff = std::chrono::seconds::zero() },
		"false" );

	runOnce( harness, harness.server.url( "/limited" ) );
	CHECK( limited_requests.load() == 2 );

	{
		const std::scoped_lock lock { arrivals_mutex };
		REQUIRE( arrivals.size() == 2 );
		CHECK( arrivals[ 1 ] - arrivals[ 0 ] >= std::chrono::milliseconds { 900 } );
	}

	REQUIRE( harness.observer.requests.size() == 1 );
	const RequestTiming& timing { harness.observer.requests.front().timing };
	CHECK( timing.attempts == 2 );
	REQUIRE( timing.queued.has_value() );
	REQUIRE( timing.sent.has_value() );
	CHECK( timing.sent.value() - timing.queued.value() >= std::chrono::milliseconds { 900 } );

	const auto lanes { harness.context->laneSnapshots() };
	REQUIRE( lanes.size() == 1 );
	CHECK_FALSE( lanes.front().backed_off );

	runOnce( harness, harness.server.url( "/fine" ) );
	idhan::test::checkNoFailures( harness.observer );
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
		R"([{"export":"fetchIt","path":"/page"}])",
		DownloaderConfig {}, R"({"requests":1,"seconds":1})" );

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

TEST_CASE( "A streamed import survives a 429 retry", "[downloader][transport]" )
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
    idhan.queueImport({request: {url: input.url}, filename: "retry.png"});
}
)JS" );
	harness.configure(
		R"([{"export":"fetchIt","path":"/file"}])",
		DownloaderConfig { .lane_error_backoff = std::chrono::seconds::zero() },
		R"({"requests":10,"seconds":1})",
		R"({"backoffStatuses":[]})" );

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
    idhan.queueImport({request: {url: new URL("/big", base).href}, filename: "clip.mp4"});
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

TEST_CASE( "An oversized page body is refused rather than buffered", "[downloader][transport][bandwidth]" )
{
	(void)takeBandwidthLogs();
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
	const auto logs { takeBandwidthLogs() };
	REQUIRE( logs.size() == 1 );
	CHECK( logs.front().host == "127.0.0.1" );
	CHECK( logs.front().bytes_received > 0 );
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
    idhan.queueImport({request: {url: new URL("/missing", base).href}, filename: "gone.png"});
}
)JS" );
	harness.configure( R"([{"export":"fetchIt","path":"/page"}])" );

	runOnce( harness, harness.server.url( "/page" ) );

	CHECK( harness.imports.stored.empty() );
	CHECK( harness.imports.aborted.load() == 1 );

	REQUIRE( harness.observer.import_failures.size() == 1 );
	CHECK( harness.observer.import_failures.front().second.contains( "404" ) );
}

TEST_CASE( "Unknown followed URLs fail on 404 without backing off their lane", "[downloader][transport][direct]" )
{
	Harness harness {};
	test::TestResponse missing {
		.status = 404,
		.reason = "Not Found",
		.headers = { { "Content-Type", "text/html" }, { "Retry-After", "7200" } },
		.body = "<html>no such file</html>"
	};
	std::string target { "/missing" };

	SECTION( "A missing file" )
	{}
	SECTION( "A redirect to a missing file" )
	{
		target = "/follow";
		harness.server.route(
			"/follow",
			[]( const test::TestRequest& )
			{
				return test::TestResponse {
					.status = 302, .reason = "Found", .headers = { { "Location", "/missing" } }
				};
			} );
	}
	SECTION( "A missing file with a malformed error body" )
	{
		// A malformed body must not replace the HTTP 404 with a curl transfer error.
		missing.headers.emplace_back( "Transfer-Encoding", "chunked" );
		missing.body = "invalid chunk\r\n";
	}

	harness.server.route( "/missing", [ &missing ]( const test::TestRequest& ) { return missing; } );
	harness.server.route(
		"/file.png",
		[]( const test::TestRequest& )
		{ return test::TestResponse { .headers = { { "Content-Type", "image/png" } }, .body = "IMAGE" }; } );

	harness.parsers.write( "test.js", R"JS(
export function gallery(input, idhan) {
    const target = new URL(input.url);
    idhan.follow({url: new URL(target.searchParams.get("target"), target).href});
}
)JS" );
	harness.configure( R"([{"export":"gallery","path":"/gallery"}])" );

	const std::string gallery { harness.server.url( "/gallery?target=" + target ) };
	runOnce( harness, gallery );
	runOnce( harness, gallery );

	REQUIRE( harness.observer.import_failures.size() == 2 );
	for ( const auto& [ url, error ] : harness.observer.import_failures )
	{
		CHECK( error.contains( "HTTP 404 (Not Found)" ) );
		CHECK_FALSE( error.contains( "single file" ) );
	}
	for ( const auto stage : harness.observer.import_failure_stages ) CHECK( stage == FailureStage::DOWNLOAD );

	CHECK( harness.imports.stored.empty() );
	CHECK( harness.imports.aborted.load() == 2 );

	const auto lanes { harness.context->laneSnapshots() };
	REQUIRE( lanes.size() == 1 );
	CHECK_FALSE( lanes.front().backed_off );

	runOnce( harness, harness.server.url( "/gallery?target=/file.png" ) );
	REQUIRE( harness.imports.stored.size() == 1 );
	CHECK( harness.imports.stored.front().second == "IMAGE" );
	CHECK( harness.observer.import_failures.size() == 2 );
	CHECK( harness.server.requestCount() == ( target == "/follow" ? 5 : 3 ) );
}

TEST_CASE( "Parser requests reject 404 by default without backing off", "[downloader][transport][404]" )
{
	Harness harness {};
	test::TestResponse missing {
		.status = 404, .reason = "Not Found", .headers = { { "Retry-After", "7200" } }, .body = "not JSON"
	};
	SECTION( "A normal error body" )
	{}
	SECTION( "A malformed error body" )
	{
		missing.headers.emplace_back( "Transfer-Encoding", "chunked" );
	}

	harness.server.route( "/missing", [ &missing ]( const test::TestRequest& ) { return missing; } );
	harness.server.route( "/after", []( const test::TestRequest& ) { return test::TestResponse { .body = "ok" }; } );
	harness.parsers.write( "test.js", R"JS(
export async function fetchIt(input, idhan) {
    const target = new URL(input.url).searchParams.get("target");
    await idhan.request({url: new URL(target, input.url).href, responseType: "json"});
}
export async function after(input, idhan) {
    const response = await idhan.request({url: new URL("/after", input.url).href});
    if (response.body !== "ok") throw new Error("Next request failed");
}
)JS" );
	harness.configure( R"([{"export":"fetchIt","path":"/page"},{"export":"after","path":"/next"}])" );
	runOnce( harness, harness.server.url( "/page?target=/missing" ) );

	REQUIRE( harness.observer.failures.size() == 1 );
	CHECK( harness.observer.failures.front().second.contains( "HTTP 404" ) );
	REQUIRE( harness.observer.requests.size() == 1 );
	CHECK( harness.observer.requests.front().bytes == 0 );
	CHECK( harness.server.requestCount() == 1 );

	const auto lanes { harness.context->laneSnapshots() };
	REQUIRE( lanes.size() == 1 );
	CHECK_FALSE( lanes.front().backed_off );

	runOnce( harness, harness.server.url( "/next" ) );
	CHECK( harness.server.requestCount() == 2 );
	CHECK( harness.observer.failures.size() == 1 );
}

TEST_CASE( "HTTP error responses remain available with an explicit override", "[downloader][transport][404]" )
{
	Harness harness {};
	harness.server.route(
		"/missing",
		[]( const test::TestRequest& )
		{ return test::TestResponse { .status = 404, .reason = "Not Found", .body = "missing" }; } );
	harness.parsers.write( "test.js", R"JS(
export async function fetchIt(input, idhan) {
    const url = new URL("/missing", input.url).href;
    const response = await idhan.request({url, allowHttpErrors: true});
    if (response.status !== 404 || response.body !== "missing") throw new Error("Override lost the response");
    const fetched = await fetch(url);
    if (fetched.status !== 404 || fetched.ok || await fetched.text() !== "missing") {
        throw new Error("Fetch lost the error response");
    }
}
)JS" );
	harness.configure( R"([{"export":"fetchIt","path":"/page"}])" );
	runOnce( harness, harness.server.url( "/page" ) );
	idhan::test::checkNoFailures( harness.observer );
	CHECK( harness.server.requestCount() == 2 );
	const auto lanes { harness.context->laneSnapshots() };
	REQUIRE( lanes.size() == 1 );
	CHECK_FALSE( lanes.front().backed_off );
}

TEST_CASE( "A retry wait holds every request on the lane", "[downloader][transport][404]" )
{
	Harness harness {};
	std::atomic_bool limited { true };
	std::mutex arrivals_mutex {};
	std::vector< std::chrono::steady_clock::time_point > arrivals {};

	const auto respond = [ & ]( const test::TestRequest& )
	{
		{
			const std::scoped_lock lock { arrivals_mutex };
			arrivals.emplace_back( std::chrono::steady_clock::now() );
		}

		if ( limited.exchange( false ) )
			return test::TestResponse { .status = 404, .reason = "Not Found", .body = "hidden rate limit" };

		return test::TestResponse { .body = "ok" };
	};

	harness.server.route( "/one", respond );
	harness.server.route( "/two", respond );
	harness.parsers.write( "test.js", R"JS(
export async function fetchIt(input, idhan) {
    const response = await idhan.request({url: input.url});
    if (response.status !== 200) throw new Error(`request ended as ${response.status}`);
}
)JS" );
	harness.configureRaw( R"({"urlClasses":[{
        "name":"test","script":"test.js","requestRate":false,
        "hosts":[{"host":"127.0.0.1","retry":[{"code":404,"count":1,"wait":1}]}],
        "routes":[{"export":"fetchIt","path":"/one"},{"export":"fetchIt","path":"/two"}]
    }]})" );

	const auto session { harness.session( harness.server.url( "/one" ) ) };
	REQUIRE( session->submit( harness.server.url( "/one" ) ).has_value() );
	REQUIRE( session->submit( harness.server.url( "/two" ) ).has_value() );
	REQUIRE( waitUntil( [ & ] { return session->idle(); } ) );
	idhan::test::checkNoFailures( harness.observer );

	const auto lanes { harness.context->laneSnapshots() };
	REQUIRE( lanes.size() == 1 );
	CHECK_FALSE( lanes.front().backed_off );
	CHECK( lanes.front().rate.requests == 0 );

	const std::scoped_lock lock { arrivals_mutex };
	REQUIRE( arrivals.size() >= 3 );
	std::ranges::sort( arrivals );
	// Requests started before the 404 came back may overlap the first; only later ones are held.
	CHECK( arrivals.back() - arrivals.front() >= std::chrono::milliseconds { 900 } );
}

TEST_CASE( "A real 404 reaches the parser once its retry waits run out", "[downloader][transport][404]" )
{
	Harness harness {};
	std::atomic_size_t attempts {};
	harness.server.route(
		"/missing",
		[ & ]( const test::TestRequest& )
		{
			++attempts;
			return test::TestResponse { .status = 404, .reason = "Not Found", .body = "missing" };
		} );
	harness.parsers.write( "test.js", passthrough_script );
	harness.configureRaw( R"({"urlClasses":[{
        "name":"test","script":"test.js","requestRate":false,
        "hosts":[{"host":"127.0.0.1","retry":[{"code":404,"count":1,"wait":1}]}],
        "routes":[{"export":"fetchIt","path":"/missing"}]
    }]})" );

	const auto started { std::chrono::steady_clock::now() };
	runOnce( harness, harness.server.url( "/missing" ) );

	CHECK( attempts == 2 );
	CHECK( std::chrono::steady_clock::now() - started >= std::chrono::milliseconds { 900 } );
	REQUIRE( harness.observer.failures.size() == 1 );
	CHECK( harness.observer.failures.front().second.contains( "404" ) );
}

TEST_CASE( "Configured 404 retries are bounded and preserve streamed imports", "[downloader][transport][404]" )
{
	Harness harness {};
	bool exhausted {};
	SECTION( "The final retry succeeds" )
	{}
	SECTION( "Every attempt returns 404" )
	{
		exhausted = true;
	}

	std::atomic_size_t attempts {};
	harness.server.route(
		"/file.png",
		[ & ]( const test::TestRequest& )
		{
			++attempts;
			if ( exhausted || attempts <= 3 )
				return test::TestResponse { .status = 404, .reason = "Not Found", .body = "missing" };

			return test::TestResponse { .headers = { { "Content-Type", "image/png" } }, .body = "IMAGE" };
		} );
	harness.parsers.write( "test.js", "export function page() {}" );
	harness.configureRaw( R"({"urlClasses":[{
        "name":"test","script":"test.js","requestRate":false,
        "hosts":[{"host":"127.0.0.1","retry":[{"code":404,"count":3}]}],
        "routes":[{"export":"page","path":"/page"}]
    }]})" );
	runOnce( harness, harness.server.url( "/file.png" ) );
	CHECK( attempts == 4 );
	const auto lanes { harness.context->laneSnapshots() };
	REQUIRE( lanes.size() == 1 );
	CHECK_FALSE( lanes.front().backed_off );

	if ( exhausted )
	{
		REQUIRE( harness.observer.import_failures.size() == 1 );
		CHECK( harness.observer.import_failures.front().second.contains( "404" ) );
		CHECK( harness.imports.stored.empty() );
		CHECK( harness.imports.aborted.load() == 1 );
		return;
	}

	REQUIRE( harness.imports.stored.size() == 1 );
	CHECK( harness.imports.stored.front().second == "IMAGE" );
	CHECK( harness.observer.import_failures.empty() );
	CHECK( harness.imports.aborted.load() == 0 );
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
    idhan.queueImport({request: {url: new URL("/download", base).href}});
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

TEST_CASE( "HTTP errors do not change rate limits unless explicitly selected", "[downloader][transport]" )
{
	Harness harness {};
	const int status { GENERATE( 403, 500, 503 ) };
	harness.server.route(
		"/error",
		[ status ]( const test::TestRequest& )
		{
			return test::TestResponse {
				.status = status, .reason = "Error", .headers = { { "Retry-After", "7200" } }, .body = "error"
			};
		} );
	harness.server.route( "/after", []( const test::TestRequest& ) { return test::TestResponse { .body = "ok" }; } );
	harness.parsers.write( "test.js", R"JS(
export async function fetchIt(input, idhan) {
    const response = await idhan.request({url: new URL("/error", input.url).href});
    if (response.status !== Number(new URL(input.url).searchParams.get("status"))) {
        throw new Error("Error response was lost");
    }
    const after = await idhan.request({url: new URL("/after", input.url).href});
    if (after.body !== "ok") throw new Error("Next request failed");
}
)JS" );
	harness.configure( R"([{"export":"fetchIt","path":"/page"}])" );
	runOnce( harness, harness.server.url( "/page?status=" + std::to_string( status ) ) );
	idhan::test::checkNoFailures( harness.observer );
	CHECK( harness.server.requestCount() == 2 );
	const auto lanes { harness.context->laneSnapshots() };
	REQUIRE( lanes.size() == 1 );
	CHECK_FALSE( lanes.front().backed_off );
}

TEST_CASE( "Transport failures do not change rate limits", "[downloader][transport]" )
{
	Harness harness {};
	harness.parsers.write( "test.js", passthrough_script );
	harness.configure( R"([{"export":"fetchIt","path":"/page"}])" );
	const auto url { harness.server.url( "/page" ) };
	harness.server.stop();
	runOnce( harness, url );
	REQUIRE( harness.observer.failures.size() == 1 );
	const auto lanes { harness.context->laneSnapshots() };
	REQUIRE( lanes.size() == 1 );
	CHECK_FALSE( lanes.front().backed_off );
}

TEST_CASE( "An explicitly configured error status stops its lane until the backoff passes", "[downloader][transport]" )
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
		DownloaderConfig { .lane_error_backoff = std::chrono::seconds { 2 } },
		"false",
		R"({"backoffStatuses":[500]})" );

	runOnce( harness, harness.server.url( "/page" ) );

	idhan::test::checkNoFailures( harness.observer );

	const std::scoped_lock lock { arrivals_mutex };
	REQUIRE( arrivals.size() == 2 );
	CHECK( arrivals[ 1 ] - arrivals[ 0 ] >= std::chrono::milliseconds { 1900 } );
}

TEST_CASE( "A rate limited lane holds requests past its allowance", "[downloader][transport]" )
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

TEST_CASE(
	"Host discovery cannot refill the global rate budget between sessions",
	"[downloader][transport][global-rate]" )
{
	Harness harness {};
	std::mutex mutex {};
	std::vector< std::chrono::steady_clock::time_point > arrivals {};
	harness.server.route(
		"/page",
		[ & ]( const test::TestRequest& )
		{
			const std::scoped_lock lock { mutex };
			arrivals.push_back( std::chrono::steady_clock::now() );
			return test::TestResponse { .body = "file" };
		} );
	harness.parsers.write( "test.js", R"JS(
export async function fetchIt(input, host) {
    await host.learnHost(new URL(input.url).origin, 'test');
    if (input.url.includes('import')) {
        host.queueImport({request: {url: input.url}});
    } else {
        await host.request({url: input.url});
    }
}
)JS" );
	harness.configure(
		R"([{"export":"fetchIt","path":"/page"}])",
		DownloaderConfig { .parser_instances = 2 },
		R"({"requests":1,"seconds":1})" );
	std::string mode;
	bool concurrent {};
	SECTION( "Parser requests in sequential sessions" )
	{
		mode = "request";
	}
	SECTION( "Queued imports in sequential sessions" )
	{
		mode = "import";
	}
	SECTION( "Parser requests in concurrent sessions" )
	{
		mode = "request";
		concurrent = true;
	}
	SECTION( "Queued imports in concurrent sessions" )
	{
		mode = "import";
		concurrent = true;
	}
	const std::vector sessions { harness.session( "" ), harness.session( "" ) };
	for ( int index {}; index < 2; ++index )
	{
		const auto& session { sessions[ index ] };
		REQUIRE( session->submit( harness.server.url( "/page?" + mode + std::to_string( index ) ) ).has_value() );
		if ( !concurrent ) folly::coro::blockingWait( session->wait() );
	}
	for ( const auto& session : sessions ) folly::coro::blockingWait( session->wait() );

	idhan::test::checkNoFailures( harness.observer );
	const std::scoped_lock lock { mutex };
	REQUIRE( arrivals.size() == 2 );
	CHECK( arrivals[ 1 ] - arrivals[ 0 ] >= std::chrono::milliseconds { 900 } );
}

TEST_CASE(
	"A 429 pauses the shared lane before waiting for completion workers",
	"[downloader][transport][global-rate]" )
{
	test::TestServer server;
	std::atomic_bool release {};
	std::atomic_bool worker_blocked {};
	std::atomic_size_t attempts {};
	std::atomic_size_t completed {};
	std::atomic_size_t next_requests {};
	server.route( "/block", []( const test::TestRequest& ) { return test::TestResponse { .body = "ok" }; } );
	server.route(
		"/limited",
		[ &attempts ]( const test::TestRequest& )
		{
			if ( ++attempts == 1 )
				return test::TestResponse {
					.status = 429, .reason = "Too Many Requests", .headers = { { "Retry-After", "1" } }
				};

			return test::TestResponse { .body = "ok" };
		} );
	server.route(
		"/next",
		[ &next_requests ]( const test::TestRequest& )
		{
			++next_requests;
			return test::TestResponse { .body = "ok" };
		} );
	LanePool::Config config;
	config.http_version = HttpVersion::HTTP_1_1;
	config.import_threads = 1;
	LanePool pool { config, []( std::string_view ) { return LaneSettings { .concurrency = 1 }; }, 1 };
	TransferRequest first;
	first.url = server.url( "/block" );
	pool.send(
		std::move( first ),
		[ & ]( TransferResult )
		{
			worker_blocked = true;
			while ( !release.load() ) std::this_thread::sleep_for( std::chrono::milliseconds { 5 } );

			++completed;
		} );
	const bool blocked { waitUntil( [ & ] { return worker_blocked.load(); } ) };
	TransferRequest limited;
	limited.url = server.url( "/limited" );
	pool.send( std::move( limited ), [ &completed ]( TransferResult ) { ++completed; } );
	TransferRequest next;
	next.url = server.url( "/next" );
	pool.send( std::move( next ), [ &completed ]( TransferResult ) { ++completed; } );
	const bool observed { waitUntil(
		[ & ]
		{
			const auto snapshots { pool.snapshots() };
			return !snapshots.empty() && ( snapshots.front().backed_off || next_requests.load() != 0 );
		} ) };
	const auto snapshots { pool.snapshots() };
	const auto early_requests { next_requests.load() };
	release = true;
	const bool drained { waitUntil( [ & ] { return completed.load() == 3; } ) };
	folly::coro::blockingWait( pool.shutdown() );
	REQUIRE( blocked );
	REQUIRE( observed );
	REQUIRE( snapshots.size() == 1 );
	CHECK( snapshots.front().backed_off );
	CHECK( early_requests == 0 );
	CHECK( drained );
	CHECK( attempts.load() == 2 );
}

TEST_CASE( "A rate limited lane holds import requests past its allowance before dispatch", "[downloader][transport]" )
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
        idhan.queueImport({request: {url: new URL(`/file/${id}`, base).href}});
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

TEST_CASE(
	"Session network speed counts only its own traffic and expires after idle",
	"[downloader][transport][network-stats]" )
{
	Harness harness {};
	harness.server.route(
		"/page", []( const test::TestRequest& ) { return test::TestResponse { .body = std::string( 4096, 'x' ) }; } );
	harness.parsers.write( "test.js", passthrough_script );
	harness.configure( R"([{"export":"fetchIt","path":"/page"}])" );
	const auto active { harness.session( "" ) };
	const auto other { harness.session( "" ) };
	REQUIRE( active->submit( harness.server.url( "/page" ) ).has_value() );
	folly::coro::blockingWait( active->wait() );
	idhan::test::checkNoFailures( harness.observer );
	const auto network { active->snapshot() };
	CHECK( network.downloading == 0 );
	CHECK( network.waiting == 0 );
	CHECK( network.waiting_http_rate == 0 );
	CHECK( network.waiting_http_shard == 0 );
	CHECK( network.waiting_storage == 0 );
	REQUIRE( waitUntil( [ & ] { return active->snapshot().download_bytes_per_second > 0; } ) );
	CHECK( active->snapshot().download_bytes_per_second <= 4096 );
	CHECK( network.downloaded_bytes == 4096 );
	CHECK( network.average_import_bytes == 0 );
	CHECK( other->snapshot().downloaded_bytes == 0 );
	CHECK( other->snapshot().download_bytes_per_second == 0 );
	CHECK( waitUntil( [ & ] { return active->snapshot().download_bytes_per_second == 0; } ) );
	CHECK( active->snapshot().downloaded_bytes == 4096 );
}

TEST_CASE( "Session waits report rate and shard queues separately", "[downloader][transport][network-stats]" )
{
	Harness harness {};
	std::atomic_bool release {};
	harness.server.route(
		"/page",
		[ & ]( const test::TestRequest& )
		{
			while ( !release.load() ) std::this_thread::sleep_for( std::chrono::milliseconds { 2 } );

			return test::TestResponse { .body = "ok" };
		} );
	harness.parsers.write( "test.js", passthrough_script );
	harness.configureRaw(
		R"({"urlClasses":[
		{"name":"rate","script":"test.js","requestRate":{"requests":1,"seconds":10,"concurrency":2},
		 "hosts":["localhost"],"routes":[{"export":"fetchIt","path":"/page"}]},
		{"name":"shard","script":"test.js","requestRate":false,"lane":{"concurrency":1},
		 "hosts":["127.0.0.1"],"routes":[{"export":"fetchIt","path":"/page"}]}
	]})",
		DownloaderConfig { .io_threads = 1 } );
	const auto session { harness.session( "" ) };
	for ( int index {}; index < 2; ++index )
	{
		const auto url { harness.server.url( "/page?i=" + std::to_string( index ) ) };
		std::string rate_url { url };
		rate_url.replace( rate_url.find( "127.0.0.1" ), 9, "localhost" );
		REQUIRE( session->submit( url ).has_value() );
		REQUIRE( session->submit( rate_url ).has_value() );
	}
	const bool separated { waitUntil(
		[ & ]
		{
			const auto snapshot { session->snapshot() };
			return snapshot.waiting_http_rate == 1 && snapshot.waiting_http_shard == 1;
		} ) };
	const auto snapshot { session->snapshot() };
	release.store( true );
	session->cancel();
	folly::coro::blockingWait( session->wait() );

	REQUIRE( separated );
	CHECK( snapshot.downloading == 2 );
	CHECK( snapshot.waiting_requests == 4 );
	CHECK( snapshot.waiting == 2 );
	CHECK( snapshot.waiting_storage == 0 );
	CHECK( snapshot.waiting_parsers == 0 );
}

TEST_CASE( "Bandwidth counts encoded response bytes", "[downloader][transport][bandwidth]" )
{
	(void)takeBandwidthLogs();
	Harness harness {};
	const std::string compressed {
		"\x1f\x8b\x08\x00\x00\x00\x00\x00\x02\xff\xed\xc1\x01\x0d\x00\x00\x00\xc2\xa0\xda\x8f\x6f\x0f\x07\x14\x00\x00\x00\xf0\x6e\xc1\x77\x10\x3e\x00\x10\x00\x00",
		38
	};
	harness.server.route(
		"/page",
		[ & ]( const test::TestRequest& )
		{ return test::TestResponse { .headers = { { "Content-Encoding", "gzip" } }, .body = compressed }; } );
	harness.parsers.write( "test.js", R"JS(
export async function fetchIt(input, idhan) {
    const response = await idhan.request({url: input.url});
    if (response.body.length !== 4096) throw new Error(`body length was ${response.body.length}`);
}
)JS" );
	harness.configure( R"([{"export":"fetchIt","path":"/page"}])" );
	runOnce( harness, harness.server.url( "/page" ) );
	idhan::test::checkNoFailures( harness.observer );
	const auto logs { takeBandwidthLogs() };
	REQUIRE( logs.size() == 1 );
	const auto header { std::string { "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: " }
		                + std::to_string( compressed.size() ) + "\r\nConnection: keep-alive\r\n\r\n" };
	CHECK( logs.front().bytes_received == static_cast< std::int64_t >( header.size() + compressed.size() ) );
}

TEST_CASE(
	"A disabled lane fails queued and new requests until a user resets it",
	"[downloader][transport][429-recovery]" )
{
	using namespace std::chrono_literals;
	test::TestServer server;
	std::atomic_size_t requests {};
	server.route(
		"/ok",
		[ & ]( const test::TestRequest& )
		{
			++requests;
			return test::TestResponse { .body = "ok" };
		} );
	IoPool pool { 1 };
	LanePolicy policy { "host", LaneSettings {} };
	auto lane { std::make_shared< Lane >( "host", policy, pool, Lane::Config {} ) };
	policy.hold( 3600s );
	std::promise< TransferResult > queued;
	auto queued_result { queued.get_future() };
	TransferRequest first;
	first.url = server.url( "/ok" );
	first.options.http_version = HttpVersion::HTTP_1_1;
	lane->submit( std::move( first ), [ & ]( TransferResult result ) { queued.set_value( std::move( result ) ); } );
	auto now { LanePolicy::SteadyClock::now() + 3600s };
	for ( const auto delay : { 1s, 2s, 4s, 8s, 16s, 32s, 60s } )
	{
		policy.limited( std::nullopt, now );
		now += delay;
	}
	policy.limited( std::nullopt, now );
	REQUIRE( policy.disabled() );
	lane->wake();
	REQUIRE( queued_result.wait_for( 2s ) == std::future_status::ready );
	auto failed_e { queued_result.get() };
	REQUIRE_FALSE( failed_e.has_value() );
	REQUIRE( failed_e.error().code == TransferErrorCode::RATE_LIMITED );
	REQUIRE( requests.load() == 0 );

	std::promise< TransferResult > rejected;
	auto rejected_result { rejected.get_future() };
	TransferRequest second;
	second.url = server.url( "/ok" );
	lane->submit( std::move( second ), [ & ]( TransferResult result ) { rejected.set_value( std::move( result ) ); } );
	REQUIRE( rejected_result.wait_for( 2s ) == std::future_status::ready );
	auto rejected_e { rejected_result.get() };
	REQUIRE_FALSE( rejected_e.has_value() );
	REQUIRE( rejected_e.error().code == TransferErrorCode::RATE_LIMITED );
	REQUIRE( requests.load() == 0 );

	policy.reset();
	lane->wake();
	std::promise< TransferResult > resumed;
	auto resumed_result { resumed.get_future() };
	TransferRequest third;
	third.url = server.url( "/ok" );
	third.options.http_version = HttpVersion::HTTP_1_1;
	lane->submit( std::move( third ), [ & ]( TransferResult result ) { resumed.set_value( std::move( result ) ); } );
	REQUIRE( resumed_result.wait_for( 5s ) == std::future_status::ready );
	auto resumed_e { resumed_result.get() };
	REQUIRE( resumed_e.has_value() );
	REQUIRE( resumed_e.value().status == 200 );
	REQUIRE( requests.load() == 1 );
	lane->shutdown();
	pool.stop();
}

TEST_CASE( "Request logs separate multi-minute queue waits from network transfer time", "[downloader][request-timing]" )
{
	using namespace std::chrono_literals;
	const LanePolicy::SteadyClock::time_point start { 100s };
	const auto sent { start + 28ms + 282s };
	const auto wall { std::chrono::sys_days { std::chrono::year { 2026 } / 11 / 12 } + 21h + 33min + 24s + 432ms };
	RequestTiming timing {
		.made = start, .queued = start + 28ms, .sent = sent, .sent_wall = wall, .received = sent + 1200ms, .attempts = 1
	};
	const auto description { describeRequestTiming( timing ) };
	REQUIRE( description.find( "sent at 2026-11-12 21:33:24.432 UTC" ) != std::string::npos );
	REQUIRE( description.find( "transfer 1.2s" ) != std::string::npos );
	REQUIRE( description.find( "wait before send 4.7min" ) != std::string::npos );
	REQUIRE( description.find( "sent +4.7min" ) == std::string::npos );
	REQUIRE( description.find( "total 4.7min" ) == std::string::npos );
	timing.sent.reset();
	timing.sent_wall.reset();
	timing.received.reset();
	REQUIRE( describeRequestTiming( timing ) == "not sent, preparation 28.0ms" );
}

TEST_CASE( "A failed connection never claims its request was sent", "[downloader][transport][request-timing]" )
{
	using namespace std::chrono_literals;
	test::TestServer server;
	const auto url { server.url( "/closed" ) };
	server.stop();
	IoPool pool { 1 };
	LanePolicy policy { "host", LaneSettings {} };
	auto lane { std::make_shared< Lane >( "host", policy, pool, Lane::Config {} ) };
	const auto progress { std::make_shared< NetworkActivity::Request >() };
	std::promise< TransferResult > completed;
	auto result { completed.get_future() };
	TransferRequest request;
	request.url = url;
	request.options.http_version = HttpVersion::HTTP_1_1;
	request.options.timeout_ms = 1000;
	request.progress = progress;
	lane->submit(
		std::move( request ), [ & ]( TransferResult response ) { completed.set_value( std::move( response ) ); } );
	REQUIRE( result.wait_for( 5s ) == std::future_status::ready );
	const auto result_e { result.get() };
	REQUIRE_FALSE( result_e.has_value() );
	const auto timing { progress->timing( LanePolicy::SteadyClock::now() ) };
	REQUIRE( timing.queued.has_value() );
	REQUIRE_FALSE( timing.sent.has_value() );
	REQUIRE_FALSE( timing.sent_wall.has_value() );
	lane->shutdown();
	pool.stop();
}

TEST_CASE(
	"A queued request records the outgoing headers rather than its enqueue time",
	"[downloader][transport][request-timing]" )
{
	using namespace std::chrono_literals;
	test::TestServer server;
	std::chrono::system_clock::time_point arrived;
	server.route(
		"/ok",
		[ & ]( const test::TestRequest& )
		{
			arrived = std::chrono::system_clock::now();
			return test::TestResponse { .body = "ok" };
		} );
	IoPool pool { 1 };
	LanePolicy policy { "host", LaneSettings {} };
	auto lane { std::make_shared< Lane >( "host", policy, pool, Lane::Config {} ) };
	policy.hold( 1s );
	const auto made { LanePolicy::SteadyClock::now() };
	const auto progress { std::make_shared< NetworkActivity::Request >() };
	std::promise< TransferResult > completed;
	auto result { completed.get_future() };
	TransferRequest request;
	request.url = server.url( "/ok" );
	request.options.http_version = HttpVersion::HTTP_1_1;
	request.progress = progress;
	lane->submit(
		std::move( request ), [ & ]( TransferResult response ) { completed.set_value( std::move( response ) ); } );
	REQUIRE_FALSE( progress->timing( made ).sent.has_value() );
	REQUIRE( result.wait_for( 5s ) == std::future_status::ready );
	const auto result_e { result.get() };
	REQUIRE( result_e.has_value() );
	const auto timing { progress->timing( made ) };
	REQUIRE( timing.queued.has_value() );
	REQUIRE( timing.sent.has_value() );
	REQUIRE( timing.sent_wall.has_value() );
	REQUIRE( timing.received.has_value() );
	REQUIRE( timing.sent.value() - timing.queued.value() >= 900ms );
	REQUIRE( timing.sent_wall.value() <= arrived );
	REQUIRE( arrived - timing.sent_wall.value() < 500ms );
	lane->shutdown();
	pool.stop();
}
