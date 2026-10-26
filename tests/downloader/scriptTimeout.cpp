#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>

#include "HostCallback.hpp"
#include "harness/Harness.hpp"
#include "js/ScriptContext.hpp"
#include "js/bindings/IdhanBindings.hpp"
#include "scripts/BytecodeCache.hpp"

namespace idhan::test
{

class DelayedImportObserver final : public downloader::SessionObserver
{
  public:

	RecordingObserver recording {};
	std::size_t queued {};
	bool reject {};

	std::expected< void, std::string > onImportQueued( const downloader::WorkInfo& ) override
	{
		++queued;
		std::this_thread::sleep_for( std::chrono::milliseconds { 300 } );
		if ( reject ) throw std::runtime_error( "delayed database failure" );

		return {};
	}

	void onCompleted( const downloader::WorkInfo& info ) override { recording.onCompleted( info ); }

	void onFailed( const downloader::WorkInfo& info, const std::string& error ) override
	{
		recording.onFailed( info, error );
	}

	void onImported( const downloader::ImportInfo& info ) override { recording.onImported( info ); }

	void onImportFailed( const downloader::WorkInfo& info, const std::string& url, const std::string& error ) override
	{
		recording.onImportFailed( info, url, error );
	}
};

TEST_CASE( "Blocking import callbacks do not consume the JavaScript budget", "[downloader][timeout]" )
{
	const bool reject { GENERATE( false, true ) };
	DelayedImportObserver observer {};
	Harness harness {};
	observer.reject = reject;

	harness.server.route(
		"/file",
		[]( const TestRequest& )
		{
			std::this_thread::sleep_for( std::chrono::milliseconds { 300 } );
			return TestResponse { .body = "file" };
		} );
	harness.parsers.write( "test.js", R"JS(
export async function run(input, idhan) {
    await idhan.request({url: input.url});
    for (let i = 0; i < 3; ++i) {
        idhan.import({request: {url: input.url}});
        // Exercise interrupt polling after every blocking callback, as Gelbooru's tag parser does.
        for (let tag = 0; tag < 20000; ++tag) "cat blue_eyes".split(" ");
    }
    if (input.url.endsWith("?runaway")) while (true) {}
}
)JS" );
	harness.configure(
		R"([{"export":"run","path":"/file"}])",
		downloader::DownloaderConfig { .script_burst_timeout = std::chrono::milliseconds { 200 } } );
	const auto session { harness.context->createSession( downloader::SessionOptions { .observer = &observer } ) };
	REQUIRE( session->submit( harness.server.url( "/file" ) ).has_value() );
	REQUIRE( waitUntil( [ & ] { return session->idle(); } ) );
	checkNoFailures( observer.recording );
	CHECK( observer.queued == 3 );
	CHECK( observer.recording.completed.size() == 1 );
	CHECK( observer.recording.imports.size() == ( reject ? 0 : 3 ) );
	CHECK( observer.recording.import_failures.size() == ( reject ? 3 : 0 ) );

	// A host pause must preserve the remaining budget and restore the interrupt handler after errors.
	REQUIRE( session->submit( harness.server.url( "/file?runaway" ) ).has_value() );
	REQUIRE( waitUntil( [ & ] { return session->idle(); } ) );
	REQUIRE( observer.recording.failures.size() == 1 );
	CHECK( observer.recording.failures.front().second.contains( "interrupted" ) );
	harness.context->shutdown();
}

void rememberTimeoutRejection(
	JSContext* context,
	JSValueConst,
	JSValueConst reason,
	const int is_handled,
	void* opaque )
{
	if ( is_handled != 0 ) return;

	auto* rejection { static_cast< JSValue* >( opaque ) };
	if ( !JS_IsUndefined( *rejection ) ) return;

	*rejection = JS_DupValue( context, reason );
}

TEST_CASE( "Endless promise chains still exhaust the JavaScript budget", "[downloader][timeout]" )
{
	ParserFixture parsers {};
	downloader::BytecodeCache bytecode { parsers.root() };
	downloader::ScriptContext scripts { { .burst_timeout = std::chrono::milliseconds { 200 } }, bytecode };
	downloader::JSContextPtr context { JS_NewContext( scripts.runtime() ) };
	REQUIRE( context != nullptr );
	JSValue rejection { JS_UNDEFINED };
	// Interruptions are uncatchable in JavaScript, so retain the native rejection until the burst ends.
	JS_SetHostPromiseRejectionTracker( scripts.runtime(), rememberTimeoutRejection, &rejection );
	const char* source { R"JS(
function again() {
    return Promise.resolve().then(again);
}
again();
)JS" };
	JSValue evaluated { JS_Eval( context.get(), source, std::strlen( source ), "jobs.js", JS_EVAL_TYPE_GLOBAL ) };
	REQUIRE_FALSE( JS_IsException( evaluated ) );
	JS_FreeValue( context.get(), evaluated );
	std::string error {};
	scripts.pumpJobs( error );
	INFO( error );
	const std::string rejection_text { downloader::scriptValueString( context.get(), rejection ) };
	INFO( rejection_text );
	CHECK( ( error.contains( "interrupted" ) || rejection_text.contains( "interrupted" ) ) );
	JS_SetHostPromiseRejectionTracker( scripts.runtime(), nullptr, nullptr );
	JS_FreeValue( context.get(), rejection );
}

TEST_CASE( "Nested host callbacks preserve the remaining JavaScript budget", "[downloader][timeout]" )
{
	ParserFixture parsers {};
	downloader::BytecodeCache bytecode { parsers.root() };
	downloader::ScriptContext scripts { { .burst_timeout = std::chrono::milliseconds { 200 } }, bytecode };
	downloader::JSContextPtr context { JS_NewContext( scripts.runtime() ) };
	REQUIRE( context != nullptr );
	scripts.enterBurst();
	const auto callback_e { downloader::hostCallback(
		[]
		{
			const auto nested_e {
				downloader::hostCallback( [] { std::this_thread::sleep_for( std::chrono::milliseconds { 300 } ); } )
			};
			REQUIRE( nested_e.has_value() );
			std::this_thread::sleep_for( std::chrono::milliseconds { 300 } );
		} ) };
	CHECK( callback_e.has_value() );
	// The nested pause must not credit the same wait twice, or this overdue script would survive.
	std::this_thread::sleep_for( std::chrono::milliseconds { 300 } );
	const char* source { "for (let i = 0; i < 20000; ++i) {}" };
	JSValue evaluated { JS_Eval( context.get(), source, std::strlen( source ), "overdue.js", JS_EVAL_TYPE_GLOBAL ) };
	scripts.leaveBurst();
	CHECK( JS_IsException( evaluated ) );
	JS_FreeValue( context.get(), evaluated );
	JS_FreeValue( context.get(), JS_GetException( context.get() ) );
}

} // namespace idhan::test
