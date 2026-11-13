#include <catch2/catch_test_macros.hpp>
#include <folly/ScopeGuard.h>
#include <folly/coro/Baton.h>
#include <folly/coro/BlockingWait.h>

#include <atomic>
#include <mutex>
#include <set>

#include "harness/Harness.hpp"

namespace idhan::test
{

class HostSchedulingObserver final : public downloader::SessionObserver
{
	std::mutex m_mutex {};
	std::set< downloader::WorkID > m_started {};

  public:

	folly::coro::Baton resume {};
	std::atomic_bool entered {};
	std::atomic_bool invalid_parent {};
	std::atomic_bool fast_imported {};
	std::atomic_size_t imported {};
	std::atomic_size_t failures {};

	folly::coro::Task< void > onStarted( downloader::WorkInfo info ) override
	{
		if ( info.url.ends_with( "/blocked" ) )
		{
			entered.store( true );
			co_await resume;
		}
		const std::scoped_lock lock { m_mutex };
		m_started.insert( info.id );
	}

	folly::coro::Task< std::expected< void, std::string > > onImportQueued( downloader::WorkInfo info ) override
	{
		const std::scoped_lock lock { m_mutex };
		if ( !info.parent.has_value() || !m_started.contains( info.parent.value() ) )
		{
			invalid_parent.store( true );
			co_return std::unexpected( "Import ran before its producer's start callback completed" );
		}
		co_return std::expected< void, std::string > {};
	}

	folly::coro::Task< void > onImported( downloader::ImportInfo info ) override
	{
		++imported;
		if ( info.filename == "fast.bin" ) fast_imported.store( true );

		co_return;
	}

	folly::coro::Task< void > onFailed( downloader::WorkInfo, std::string ) override
	{
		++failures;
		co_return;
	}

	folly::coro::Task< void > onImportFailed( downloader::WorkInfo, std::string, std::string ) override
	{
		++failures;
		co_return;
	}
};

void writeHostSchedulingParser( Harness& harness )
{
	harness.parsers.write( "test.js", R"JS(
export default class Test {
    static metadata = {id: 'host-scheduling', version: 1, sdkVersion: '1.1.0',
        options: [], handlers: [], discoveries: [], urlClasses: []};
}
export async function run(input, host) {
    const path = new URL(input.url).pathname;
    const slow = path === '/delay' ? host.delay(30000).catch(() => {}) : undefined;
    if (slow) await host.delay(0);
    const filename = path === '/slow' ? 'slow.bin' : 'fast.bin';
    await host.importDirect({id: 1, operation: 'open', filename,
        urls: [{url: input.url, type: 'post'}], tags: {}, metadata: {}});
    await host.importDirect({id: 1, operation: 'write'}, new Uint8Array([42]));
    await host.importDirect({id: 1, operation: 'finish'});
    if (slow) await slow;
}
)JS" );
}

TEST_CASE( "A long delay does not block host operations in the same invocation", "[downloader][host-scheduling]" )
{
	HostSchedulingObserver observer;
	Harness harness;
	writeHostSchedulingParser( harness );
	downloader::DownloaderConfig config;
	config.import_threads = 1;
	harness.configure( R"([{"pattern":"/.*","export":"run"}])", config );
	const auto session { harness.context->createSession( downloader::SessionOptions { .observer = &observer } ) };
	const auto cleanup { folly::makeGuard(
		[ & ]
		{
			session->cancel();
			folly::coro::blockingWait( harness.context->shutdown() );
		} ) };
	REQUIRE( session->submit( harness.server.url( "/delay" ) ).has_value() );
	REQUIRE( waitUntil( [ & ] { return observer.fast_imported.load(); }, std::chrono::seconds { 2 } ) );
	CHECK_FALSE( session->idle() );
	CHECK_FALSE( observer.invalid_parent.load() );
	session->cancel();
	REQUIRE( waitUntil( [ & ] { return session->idle(); } ) );
	CHECK( observer.failures.load() == 0 );
}

TEST_CASE( "A blocked observer does not block another invocation in the same session", "[downloader][host-scheduling]" )
{
	HostSchedulingObserver observer;
	Harness harness;
	writeHostSchedulingParser( harness );
	downloader::DownloaderConfig config;
	config.import_threads = 1;
	harness.configure( R"([{"pattern":"/.*","export":"run"}])", config );
	const auto session { harness.context->createSession( downloader::SessionOptions { .observer = &observer } ) };
	const auto cleanup { folly::makeGuard(
		[ & ]
		{
			observer.resume.post();
			session->cancel();
			folly::coro::blockingWait( harness.context->shutdown() );
		} ) };
	REQUIRE( session->submit( harness.server.url( "/blocked" ) ).has_value() );
	REQUIRE( waitUntil( [ & ] { return observer.entered.load(); } ) );
	REQUIRE( session->submit( harness.server.url( "/fast" ) ).has_value() );
	REQUIRE( waitUntil( [ & ] { return observer.fast_imported.load(); }, std::chrono::seconds { 2 } ) );
	CHECK( observer.imported.load() == 1 );
	observer.resume.post();
	REQUIRE( waitUntil( [ & ] { return session->idle(); } ) );
	CHECK( observer.imported.load() == 2 );
	CHECK_FALSE( observer.invalid_parent.load() );
	CHECK( observer.failures.load() == 0 );
}

class HostSchedulingImports final : public downloader::ImportSinkFactory
{
	class Sink final : public downloader::ImportSink
	{
		HostSchedulingImports& m_owner;

	  public:

		explicit Sink( HostSchedulingImports& owner ) : m_owner( owner ) {}

		folly::coro::Task< std::expected< void, std::string > > write( std::span< const std::byte > ) override
		{
			co_return std::expected< void, std::string > {};
		}

		folly::coro::Task< std::expected< downloader::ImportResult, std::string > > finish(
			downloader::ImportMetadata metadata ) override
		{
			if ( metadata.filename == "slow.bin" )
			{
				m_owner.entered.store( true );
				co_await m_owner.resume;
			}
			co_return downloader::ImportResult {};
		}

		void abort() override {}
	};

  public:

	folly::coro::Baton resume {};
	std::atomic_bool entered {};

	std::expected< std::unique_ptr< downloader::ImportSink >, std::string > open( const downloader::ImportRequest& )
		override
	{
		return std::make_unique< Sink >( *this );
	}
};

TEST_CASE( "A slow direct import finalizer does not block another producer", "[downloader][host-scheduling]" )
{
	HostSchedulingObserver observer;
	HostSchedulingImports imports;
	Harness harness;
	writeHostSchedulingParser( harness );
	harness.configure( R"([{"pattern":"/.*","export":"run"}])" );
	downloader::DownloaderConfig config;
	config.parser_directory = harness.parsers.root();
	config.url_classes = harness.parsers.urlClassesPath();
	config.watch_scripts = false;
	config.import_threads = 1;
	const auto created_e { folly::coro::blockingWait(
		downloader::DownloaderContext::create( config, downloader::DownloaderHost { .imports = &imports } ) ) };
	REQUIRE( created_e.has_value() );
	const auto& context { created_e.value() };
	const auto session { context->createSession( downloader::SessionOptions { .observer = &observer } ) };
	const auto cleanup { folly::makeGuard(
		[ & ]
		{
			imports.resume.post();
			session->cancel();
			folly::coro::blockingWait( context->shutdown() );
		} ) };
	REQUIRE( session->submit( harness.server.url( "/slow" ) ).has_value() );
	REQUIRE( waitUntil( [ & ] { return imports.entered.load(); } ) );
	REQUIRE( session->submit( harness.server.url( "/fast" ) ).has_value() );
	REQUIRE( waitUntil( [ & ] { return observer.fast_imported.load(); }, std::chrono::seconds { 2 } ) );
	CHECK( observer.imported.load() == 1 );
	imports.resume.post();
	REQUIRE( waitUntil( [ & ] { return session->idle(); } ) );
	CHECK( observer.imported.load() == 2 );
	CHECK_FALSE( observer.invalid_parent.load() );
	CHECK( observer.failures.load() == 0 );
}

class SuspendedStoredRecordImports final : public downloader::ImportSinkFactory
{
	MemoryImportFactory m_imports {};

  public:

	folly::coro::Baton resume {};
	std::atomic_bool entered {};

	folly::coro::Task< std::expected< std::optional< RecordID >, std::string > > findStoredRecord(
		downloader::ImportRequest request ) override
	{
		if ( request.url.ends_with( "/slow" ) )
		{
			entered.store( true );
			co_await resume;
		}

		co_return RecordID { 1 };
	}

	std::expected< std::unique_ptr< downloader::ImportSink >, std::string > open(
		const downloader::ImportRequest& request ) override
	{
		return m_imports.open( request );
	}
};

TEST_CASE( "A suspended reuse lookup lets another import complete", "[downloader][host-scheduling][reuse]" )
{
	SuspendedStoredRecordImports imports;
	Harness harness;
	harness.parsers.write( "test.js", R"JS(
export default class Test {
    static metadata = {id: 'reuse-scheduling', version: 1, sdkVersion: '1.1.0',
        options: [], handlers: [], discoveries: [], urlClasses: []};
}
export async function run(input, host) {
    await host.queueImport({request: {url: input.url}}).importFinished;
}
)JS" );
	harness.configure( R"([{"pattern":"/.*","export":"run"}])" );
	downloader::DownloaderConfig config;
	config.parser_directory = harness.parsers.root();
	config.url_classes = harness.parsers.urlClassesPath();
	config.watch_scripts = false;
	config.import_threads = 1;
	const auto created_e { folly::coro::blockingWait(
		downloader::DownloaderContext::create( config, downloader::DownloaderHost { .imports = &imports } ) ) };
	REQUIRE( created_e.has_value() );
	const auto& context { created_e.value() };
	const auto session { context->createSession( {} ) };
	const auto cleanup { folly::makeGuard(
		[ & ]
		{
			imports.resume.post();
			session->cancel();
			folly::coro::blockingWait( context->shutdown() );
		} ) };
	REQUIRE( session->submit( harness.server.url( "/slow" ) ).has_value() );
	REQUIRE( waitUntil( [ & ] { return imports.entered.load(); } ) );
	REQUIRE( session->submit( harness.server.url( "/fast" ) ).has_value() );
	REQUIRE( waitUntil( [ & ] { return session->snapshot().counters.imported == 1; }, std::chrono::seconds { 2 } ) );
	CHECK( harness.server.requestCount() == 0 );

	imports.resume.post();
	REQUIRE( waitUntil( [ & ] { return session->idle(); } ) );
	CHECK( session->snapshot().counters.imported == 2 );
	CHECK( session->snapshot().counters.import_failed == 0 );
	CHECK( harness.server.requestCount() == 0 );
}

} // namespace idhan::test
