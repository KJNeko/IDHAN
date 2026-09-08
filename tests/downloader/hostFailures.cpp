#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <atomic>
#include <stdexcept>
#include "harness/Harness.hpp"

namespace idhan::test
{
using namespace downloader;

class FailingObserver final : public SessionObserver
{
  public:
	bool reject_queue {};
	std::atomic_size_t queued {};
	std::atomic_size_t completed {};
	std::atomic_size_t imported {};
	std::atomic_size_t failed {};
	WorkID import_id {};
	std::optional< WorkID > parent {};

	void onStarted( const WorkInfo& ) override { throw std::runtime_error( "database unavailable" ); }
	std::expected< void, std::string > onImportQueued( const WorkInfo& info ) override
	{
		import_id = info.id;
		parent = info.parent;
		++queued;
		if ( reject_queue ) throw std::runtime_error( "cannot persist import" );

		return {};
	}
	void onCompleted( const WorkInfo& ) override
	{
		++completed;
		throw std::runtime_error( "completion database unavailable" );
	}
	void onImported( const ImportInfo& info ) override
	{
		if ( info.work == import_id ) ++imported;
		throw std::runtime_error( "import database unavailable" );
	}
	void onImportFailed( const WorkInfo&, const std::string&, const std::string& ) override
	{
		++failed;
		throw std::runtime_error( "failure database unavailable" );
	}
	void onFinished() override { throw std::runtime_error( "finished observer failed" ); }
};

TEST_CASE( "Observer exceptions do not kill a worker or lose settlement", "[downloader][host]" )
{
	const bool reject { GENERATE( false, true ) };
	Harness harness {};
	FailingObserver observer {};
	observer.reject_queue = reject;
	harness.server.route( "/file", []( const TestRequest& ) { return TestResponse { .body = "file" }; } );
	harness.parsers.write( "test.js", R"JS(
export function run(input, idhan) { idhan.import({request: {url: input.url}}); }
)JS" );
	harness.configure( R"([{"export":"run","path":"/file"}])" );
	const auto session { harness.context->createSession( SessionOptions { .observer = &observer } ) };
	const auto work { session->submit( harness.server.url( "/file" ) ) };
	REQUIRE( work.has_value() );
	session->wait();
	CHECK( session->idle() );
	CHECK( observer.queued.load() == 1 );
	CHECK( observer.completed.load() == 1 );
	CHECK( observer.parent == *work );
	CHECK( observer.import_id != *work );
	CHECK( observer.imported.load() == ( reject ? 0 : 1 ) );
	CHECK( observer.failed.load() == ( reject ? 1 : 0 ) );
	CHECK( harness.server.requestCount() == ( reject ? 0 : 1 ) );
	// Join before destroying the borrowed observer, including its onFinished callback.
	harness.context->shutdown();
}

class ThrowingImports final : public ImportSinkFactory
{
	class Sink final : public ImportSink
	{
		ThrowingImports& m_owner;
	  public:
		explicit Sink( ThrowingImports& owner ) : m_owner( owner ) {}
		std::expected< void, std::string > write( std::span< const std::byte > ) override
		{
			if ( m_owner.mode == 1 ) throw std::runtime_error( "write failed" );
			return {};
		}
		std::expected< ImportResult, std::string > finish( const ImportMetadata& ) override
		{
			throw std::runtime_error( "import database unavailable" );
		}
		void abort() override
		{
			++m_owner.aborted;
			throw std::runtime_error( "abort failed" );
		}
	};
  public:
	int mode {};
	std::atomic_size_t aborted {};
	std::expected< std::unique_ptr< ImportSink >, std::string > open( const ImportRequest& ) override
	{
		if ( mode == 0 ) throw std::runtime_error( "open failed" );
		return std::make_unique< Sink >( *this );
	}
};

TEST_CASE( "Throwing import adapters settle with failure and release the lane", "[downloader][host]" )
{
	const int mode { GENERATE( 0, 1, 2 ) };
	Harness harness {};
	ThrowingImports imports {};
	imports.mode = mode;
	harness.server.route( "/file", []( const TestRequest& ) { return TestResponse { .body = "file" }; } );
	harness.parsers.write( "test.js", R"JS(
export function run(input, idhan) { idhan.import({request: {url: input.url}}); }
)JS" );
	harness.configure( R"([{"export":"run","path":"/file"}])" );
	DownloaderConfig config {};
	config.parser_directory = harness.parsers.root();
	config.io_threads = 1;
	config.script_threads = 1;
	auto created { DownloaderContext::create( config, DownloaderHost { .imports = &imports } ) };
	REQUIRE( created.has_value() );
	auto context { std::move( *created ) };
	const auto session { context->createSession( SessionOptions { .observer = &harness.observer } ) };
	REQUIRE( session->submit( harness.server.url( "/file" ) ).has_value() );
	session->wait();
	CHECK( session->idle() );
	CHECK( harness.observer.import_failures.size() == 1 );
	CHECK( imports.aborted.load() == ( mode == 0 ? 0 : 1 ) );
	REQUIRE( waitUntil( [ & ]
	{
		for ( const auto& lane : context->laneSnapshots() )
			if ( lane.in_flight != 0 ) return false;
		return true;
	} ) );
	context->shutdown();
}
}
