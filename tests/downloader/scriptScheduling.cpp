#include <folly/coro/BlockingWait.h>
#include <catch2/catch_test_macros.hpp>
#include <folly/ScopeGuard.h>
#include <json/writer.h>

#include <condition_variable>
#include <mutex>
#include <pthread.h>
#include <set>
#include <thread>
#include <time.h>

#include "harness/Harness.hpp"

namespace idhan::test
{

//! Test-only gates hold parser threads so the global instance limit is observable.
class SchedulingSecrets final : public downloader::SecretProvider
{
	std::mutex m_mutex {};
	std::condition_variable m_wakeup {};
	std::set< std::thread::id > m_blocked {};
	std::vector< std::thread::id > m_threads {};
	std::optional< clockid_t > m_cpu_clock {};
	bool m_release_all {};

  public:

	std::expected< Json::Value, std::string > secrets( const std::string_view domain ) override
	{
		const auto thread { std::this_thread::get_id() };
		std::unique_lock lock { m_mutex };
		if ( !m_cpu_clock.has_value() )
		{
			clockid_t clock {};
			if ( pthread_getcpuclockid( pthread_self(), &clock ) == 0 ) m_cpu_clock = clock;
		}

		m_threads.push_back( thread );
		if ( domain == "localhost" )
		{
			m_blocked.insert( thread );
			m_wakeup.wait( lock, [ & ] { return m_release_all; } );
		}

		return Json::Value { Json::objectValue };
	}

	std::set< std::thread::id > blocked()
	{
		const std::scoped_lock lock { m_mutex };
		return m_blocked;
	}

	std::vector< std::thread::id > threads()
	{
		const std::scoped_lock lock { m_mutex };
		return m_threads;
	}

	std::optional< std::chrono::nanoseconds > cpuTime()
	{
		const std::scoped_lock lock { m_mutex };
		if ( !m_cpu_clock.has_value() ) return std::nullopt;

		timespec now {};
		if ( clock_gettime( m_cpu_clock.value(), &now ) != 0 ) return std::nullopt;

		return std::chrono::seconds { now.tv_sec } + std::chrono::nanoseconds { now.tv_nsec };
	}

	void releaseAll()
	{
		const std::scoped_lock lock { m_mutex };
		m_release_all = true;
		m_wakeup.notify_all();
	}
};

TEST_CASE( "All sessions share a parser's dedicated thread across I/O", "[downloader][scheduler]" )
{
	SchedulingSecrets secrets;
	Harness harness;
	harness.parsers.write( "test.js", R"JS(
export default class Test {
    static metadata = {id: 'thread-owner', version: 1, sdkVersion: '1.1.0', options: [], handlers: [], discoveries: [], urlClasses: []};
}
export async function run(input, host) {
    host.secrets();
    const marker = {value: 42};
    const response = await host.request({url: new URL('/data', input.url).href, responseType: 'text'});
    host.secrets();
    if (response.body !== 'ok' || marker.value !== 42) throw new Error('Lost suspended state');
    const digest = await crypto.subtle.digest('SHA-256', new Uint8Array([42]));
    if (digest.byteLength !== 32) throw new Error('Crypto failed after I/O');
    if (!new URLPattern({pathname: '/:name'}).test(input.url)) throw new Error('Regex binding failed after I/O');
}
)JS" );
	harness.server.route( "/data", []( const TestRequest& ) { return TestResponse { .body = "ok" }; } );
	downloader::DownloaderConfig config;
	CHECK( config.parser_instances == 1 );
	harness.configureRaw(
		R"({"urlClasses":[{"name":"test","script":"test.js","requestRate":false,"hosts":["127.0.0.1"],"routes":[{"pattern":"/.*","export":"run"}]}]})",
		config,
		&secrets );
	{
		const auto first { harness.session( harness.server.url( "/first" ) ) };
		const auto second { harness.session( harness.server.url( "/second" ) ) };
		REQUIRE( first->submit( first->rootUrl() ).has_value() );
		REQUIRE( second->submit( second->rootUrl() ).has_value() );
		REQUIRE( waitUntil( [ & ] { return first->idle() && second->idle(); } ) );
		CHECK( first->snapshot().script_worker == second->snapshot().script_worker );
	}
	// The parser survives the sessions that first used it.
	const auto third { harness.session( harness.server.url( "/third" ) ) };
	REQUIRE( third->submit( third->rootUrl() ).has_value() );
	REQUIRE( waitUntil( [ & ] { return third->idle(); } ) );
	folly::coro::blockingWait( harness.context->shutdown() );
	checkNoFailures( harness.observer );
	const auto threads { secrets.threads() };
	REQUIRE( threads.size() == 6 );
	const std::set< std::thread::id > unique { threads.begin(), threads.end() };
	CHECK( unique.size() == 1 );
}

TEST_CASE( "Parser instance limits apply globally across sessions", "[downloader][scheduler]" )
{
	bool separate_sessions {};
	std::size_t parser_count { 1 };
	std::size_t instances { 1 };
	SECTION( "One instance serializes the same parser" )
	{}
	SECTION( "One instance is shared across sessions" )
	{
		separate_sessions = true;
	}
	SECTION( "Two global instances can execute the same parser" )
	{
		separate_sessions = true;
		instances = 2;
	}
	SECTION( "Three parsers have three dedicated threads" )
	{
		separate_sessions = true;
		parser_count = 3;
	}
	SchedulingSecrets secrets;
	Harness harness;
	const auto cleanup { folly::makeGuard( [ & ] { secrets.releaseAll(); } ) };
	Json::Value root { Json::objectValue };
	root[ "urlClasses" ] = Json::Value { Json::arrayValue };
	for ( std::size_t index {}; index < parser_count; ++index )
	{
		const auto name { std::format( "parser{}", index ) };
		harness.parsers.write( name + ".js", "export default class Test { static metadata = {id: '" + name + R"JS(',
version: 1, sdkVersion: '1.1.0', options: [], handlers: [], discoveries: [], urlClasses: []}; }
export function run(input, host) { host.secrets(); }
)JS" );
		Json::Value definition { Json::objectValue };
		definition[ "name" ] = name;
		definition[ "script" ] = name + ".js";
		definition[ "requestRate" ] = false;
		definition[ "hosts" ] = Json::Value { Json::arrayValue };
		definition[ "hosts" ].append( "localhost" );
		Json::Value route { Json::objectValue };
		route[ "path" ] = "/" + name;
		route[ "export" ] = "run";
		definition[ "routes" ] = Json::Value { Json::arrayValue };
		definition[ "routes" ].append( route );
		root[ "urlClasses" ].append( definition );
	}
	downloader::DownloaderConfig config;
	config.parser_instances = instances;
	Json::StreamWriterBuilder writer;
	harness.configureRaw( Json::writeString( writer, root ), config, &secrets );
	const auto origin { std::format( "http://localhost:{}", harness.server.port() ) };
	std::vector< std::shared_ptr< downloader::SessionContext > > sessions;
	const auto session { harness.session( origin ) };
	const std::size_t submissions { std::max< std::size_t >( 2, parser_count ) };
	for ( std::size_t index {}; index < submissions; ++index )
	{
		auto handle { separate_sessions ? harness.session( origin ) : session };
		const auto url { origin + std::format( "/parser{}?work={}", index % parser_count, index ) };
		REQUIRE( handle->submit( url ).has_value() );
		sessions.push_back( std::move( handle ) );
	}
	const auto concurrent { parser_count * instances };
	REQUIRE( waitUntil( [ & ] { return secrets.blocked().size() == concurrent; } ) );
	if ( concurrent == 1 )
	{
		std::size_t queued {};
		std::set< const downloader::SessionContext* > counted;
		for ( const auto& handle : sessions )
		{
			if ( counted.insert( handle.get() ).second ) queued += handle->snapshot().queued;
		}
		CHECK( queued == 1 );
	}
	secrets.releaseAll();
	for ( const auto& handle : sessions ) REQUIRE( waitUntil( [ & ] { return handle->idle(); } ) );

	folly::coro::blockingWait( harness.context->shutdown() );
	checkNoFailures( harness.observer );
	CHECK( harness.observer.completed.size() == submissions );
}

TEST_CASE( "Closing one session preserves other work on its shared parser", "[downloader][scheduler]" )
{
	SchedulingSecrets secrets;
	Harness harness;
	const auto cleanup { folly::makeGuard( [ & ] { secrets.releaseAll(); } ) };
	harness.parsers.write( "test.js", R"JS(
export default class Test {
    static metadata = {id: 'shared-close', version: 1, sdkVersion: '1.1.0', options: [], handlers: [], discoveries: [], urlClasses: []};
}
export function run(input, host) { host.secrets(); }
)JS" );
	harness.configureRaw(
		R"({"urlClasses":[{"name":"test","script":"test.js","requestRate":false,"hosts":["localhost"],"routes":[{"pattern":"/.*","export":"run"}]}]})",
		{},
		&secrets );
	const auto origin { std::format( "http://localhost:{}", harness.server.port() ) };
	const auto first { harness.session( origin + "/first" ) };
	const auto closing { harness.session( origin + "/closing" ) };
	const auto survivor { harness.session( origin + "/survivor" ) };
	REQUIRE( first->submit( first->rootUrl() ).has_value() );
	REQUIRE( waitUntil( [ & ] { return secrets.blocked().size() == 1; } ) );
	REQUIRE( closing->submit( closing->rootUrl() ).has_value() );
	REQUIRE( survivor->submit( survivor->rootUrl() ).has_value() );
	closing->close();
	REQUIRE( waitUntil( [ & ] { return closing->idle(); } ) );
	CHECK( survivor->snapshot().queued == 1 );
	secrets.releaseAll();
	REQUIRE( waitUntil( [ & ] { return first->idle() && survivor->idle(); } ) );
	folly::coro::blockingWait( harness.context->shutdown() );
	checkNoFailures( harness.observer );
	CHECK( secrets.threads().size() == 2 );
	CHECK( harness.observer.completed.size() == 2 );
}

TEST_CASE( "Idle parser threads sleep and wake for new work", "[downloader][scheduler]" )
{
	SchedulingSecrets secrets;
	Harness harness;
	harness.parsers.write( "test.js", R"JS(
export default class Test {
    static metadata = {id: 'idle-queue', version: 1, sdkVersion: '1.1.0', options: [], handlers: [], discoveries: [], urlClasses: []};
}
export function run(input, host) { host.secrets(); }
)JS" );
	harness.configureRaw(
		R"({"urlClasses":[{"name":"test","script":"test.js","requestRate":false,"hosts":["127.0.0.1"],"routes":[{"pattern":"/.*","export":"run"}]}]})",
		{},
		&secrets );
	const auto first { harness.session( harness.server.url( "/first" ) ) };
	REQUIRE( first->submit( first->rootUrl() ).has_value() );
	REQUIRE( waitUntil( [ & ] { return first->idle(); } ) );
	folly::coro::blockingWait( first->wait() );
	const auto before { secrets.cpuTime() };
	REQUIRE( before.has_value() );
	std::this_thread::sleep_for( std::chrono::milliseconds { 100 } );
	const auto after { secrets.cpuTime() };
	REQUIRE( after.has_value() );
	// A parked thread spends essentially no CPU time during this idle interval.
	CHECK( after.value() - before.value() < std::chrono::milliseconds { 5 } );
	const auto second { harness.session( harness.server.url( "/second" ) ) };
	REQUIRE( second->submit( second->rootUrl() ).has_value() );
	REQUIRE( waitUntil( [ & ] { return second->idle(); } ) );
	folly::coro::blockingWait( harness.context->shutdown() );
	checkNoFailures( harness.observer );
	CHECK( secrets.threads().size() == 2 );
}

} // namespace idhan::test
