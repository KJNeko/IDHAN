#include "js/ScriptTiming.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <folly/ScopeGuard.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include <sstream>
#include <thread>
#include <vector>

#include "harness/Fixtures.hpp"
#include "js/JSValueHandle.hpp"
#include "js/PendingPromise.hpp"
#include "js/ScriptActivity.hpp"
#include "js/ScriptContext.hpp"
#include "js/ScriptExecution.hpp"
#include "js/bindings/IdhanBindings.hpp"

namespace idhan::test
{

struct TimingField
{
	double microseconds {};
	double precision {};
};

TimingField readTimingField( const std::string& line, const std::string& name )
{
	const auto position { line.find( name + "=" ) };
	REQUIRE( position != std::string::npos );
	const auto field { line.substr( position + name.size() + 1 ) };
	std::size_t consumed {};
	const double value { std::stod( field, &consumed ) };
	const auto suffix { field.substr( consumed ) };
	double scale { 1 };
	if ( suffix.starts_with( "ns" ) ) scale = 0.001;

	if ( suffix.starts_with( "ms" ) ) scale = 1000;

	if ( suffix.starts_with( "s" ) ) scale = 1000000;

	if ( suffix.starts_with( "min" ) ) scale = 60000000;

	if ( suffix.starts_with( "h" ) ) scale = 3600000000;

	return { value * scale, scale / 10 };
}

double timingField( const std::string& line, const std::string& name )
{
	return readTimingField( line, name ).microseconds;
}

JSValue failingTimingJob( JSContext* context, int, JSValueConst* )
{
	return JS_ThrowInternalError( context, "timing test failure" );
}

TEST_CASE( "Parser timing uses readable units with one truncated decimal", "[downloader][timing]" )
{
	using namespace std::chrono_literals;
	CHECK( downloader::formatScriptDuration( 0ns ) == "0.0ns" );
	CHECK( downloader::formatScriptDuration( 999ns ) == "999.0ns" );
	CHECK( downloader::formatScriptDuration( 1000ns ) == "1.0us" );
	CHECK( downloader::formatScriptDuration( 1750ns ) == "1.7us" );
	CHECK( downloader::formatScriptDuration( 999999ns ) == "999.9us" );
	CHECK( downloader::formatScriptDuration( 1000us ) == "1.0ms" );
	CHECK( downloader::formatScriptDuration( 1750us ) == "1.7ms" );
	CHECK( downloader::formatScriptDuration( 1750ms ) == "1.7s" );
	CHECK( downloader::formatScriptDuration( 60s ) == "1.0min" );
	CHECK( downloader::formatScriptDuration( 105s ) == "1.7min" );
	CHECK( downloader::formatScriptDuration( 1h ) == "1.0h" );
}

TEST_CASE( "Parser timing logs only run and cumulative execution time", "[downloader][timing]" )
{
	std::ostringstream output;
	auto sink { std::make_shared< spdlog::sinks::ostream_sink_mt >( output ) };
	const auto original { spdlog::default_logger() };
	auto logger { std::make_shared< spdlog::logger >( "timing-test", sink ) };
	logger->set_pattern( "%v" );
	logger->set_level( spdlog::level::debug );
	spdlog::set_default_logger( logger );
	const auto restore { folly::makeGuard( [ original ] { spdlog::set_default_logger( original ); } ) };
	ParserFixture files;
	downloader::BytecodeCache cache { files.root() };
	auto activity { std::make_shared< downloader::ScriptActivity >() };
	downloader::ScriptContext scripts { {}, cache, activity };
	downloader::ScriptExecution execution;
	execution.route.script = "timed.js";
	const auto realm_e { scripts.createRealm( execution ) };
	REQUIRE( realm_e.has_value() );
	downloader::JSContextPtr realm { realm_e.value() };
	execution.context = realm.get();
	output.str( "" );
	const std::string source { "Promise.resolve().then(() => 42)" };
	scripts.enterBurst( realm.get() );
	downloader::JSValueHandle pending {
		realm.get(), JS_Eval( realm.get(), source.data(), source.size(), "timed.js", JS_EVAL_TYPE_GLOBAL )
	};
	{
		const downloader::ScriptContext::ExecutionScope nested { realm.get() };
	}
	scripts.leaveBurst();
	REQUIRE_FALSE( JS_IsException( pending.get() ) );
	scripts.enterBurst( realm.get() );
	{
		const downloader::ScriptContext::HostCall host;
		std::this_thread::sleep_for( std::chrono::milliseconds { 20 } );
		CHECK_FALSE( activity->snapshot().executing );
	}
	scripts.leaveBurst();
	const auto before_wait { activity->snapshot().busy_ns };
	std::this_thread::sleep_for( std::chrono::milliseconds { 20 } );
	CHECK( activity->snapshot().busy_ns == before_wait );
	std::string error;
	REQUIRE( scripts.runReadyMicrotasks( error ) );
	CHECK( JS_PromiseState( realm.get(), pending.get() ) == JS_PROMISE_FULFILLED );
	std::istringstream lines { output.str() };
	std::string line;
	std::vector< std::string > records;
	while ( std::getline( lines, line ) )
	{
		if ( line.contains( "downloader script timing:" ) ) records.push_back( line );
	}
	REQUIRE( records.size() == 3 );
	double previous {};
	double precision {};
	for ( const auto& record : records )
	{
		CHECK( record.contains( "parser='timed.js'" ) );
		const auto run { readTimingField( record, "run" ) };
		const auto total { readTimingField( record, "total" ) };
		CHECK( run.microseconds > 0 );
		CHECK(
			total.microseconds
		    == Catch::Approx( previous + run.microseconds ).margin( precision + run.precision + total.precision ) );
		previous = total.microseconds;
		precision = total.precision;
	}
	CHECK_FALSE( output.str().contains( "phase=" ) );
	CHECK_FALSE( output.str().contains( "host=" ) );
	CHECK_FALSE( output.str().contains( "event=" ) );
	output.str( "" );
	REQUIRE( JS_EnqueueJob( realm.get(), failingTimingJob, 0, nullptr ) == 0 );
	CHECK_FALSE( scripts.runReadyMicrotasks( error ) );
	CHECK( error.contains( "timing test failure" ) );
	CHECK( output.str().contains( "run=" ) );
	CHECK_FALSE( activity->snapshot().executing );
}
} // namespace idhan::test

namespace idhan::test
{
TEST_CASE( "Parser totals include synchronous getters, settlement and host reentry", "[downloader][timing]" )
{
	std::ostringstream output;
	auto sink { std::make_shared< spdlog::sinks::ostream_sink_mt >( output ) };
	const auto original { spdlog::default_logger() };
	auto logger { std::make_shared< spdlog::logger >( "timing-entry-test", sink ) };
	logger->set_pattern( "%v" );
	logger->set_level( spdlog::level::debug );
	spdlog::set_default_logger( logger );
	const auto restore { folly::makeGuard( [ original ] { spdlog::set_default_logger( original ); } ) };

	ParserFixture files;
	downloader::BytecodeCache cache { files.root() };
	auto activity { std::make_shared< downloader::ScriptActivity >() };
	downloader::ScriptContext scripts { { .burst_timeout = std::chrono::milliseconds { 100 } }, cache, activity };
	downloader::ScriptExecution execution;
	execution.route.script = "getters.js";
	const auto realm_e { scripts.createRealm( execution ) };
	REQUIRE( realm_e.has_value() );
	downloader::JSContextPtr realm { realm_e.value() };
	execution.context = realm.get();

	std::string property;
	std::string phase;
	bool host_reentry {};
	bool timeout {};
	SECTION( "Promise settlement then getter" )
	{
		property = "get then()";
		phase = "settle";
	}
	SECTION( "String conversion" )
	{
		property = "toString()";
		phase = "convert";
	}
	SECTION( "JSON conversion" )
	{
		property = "toJSON()";
		phase = "json";
	}
	SECTION( "Error stack getter" )
	{
		property = "get stack()";
		phase = "error";
	}
	SECTION( "JavaScript inside a paused host callback" )
	{
		property = "toString()";
		phase = "convert";
		host_reentry = true;
	}
	SECTION( "Settlement watchdog" )
	{
		property = "get then()";
		phase = "settle";
		timeout = true;
	}

	const std::string body {
		timeout ? "while (true) {}" : "const start = Date.now(); while (Date.now() - start < 20) {} return undefined;"
	};
	const std::string source { "({" + property + " { " + body + " }})" };
	downloader::JSValueHandle value {
		realm.get(), JS_Eval( realm.get(), source.data(), source.size(), "getters.js", JS_EVAL_TYPE_GLOBAL )
	};
	REQUIRE_FALSE( JS_IsException( value.get() ) );
	output.str( "" );
	const auto before { activity->snapshot().busy_ns };
	std::this_thread::sleep_for( std::chrono::milliseconds { 20 } );
	CHECK( activity->snapshot().busy_ns == before );

	if ( phase == "settle" )
	{
		JSValue resolving[ 2 ];
		downloader::JSValueHandle promise { realm.get(), JS_NewPromiseCapability( realm.get(), resolving ) };
		downloader::PendingPromise pending { realm.get(), resolving[ 0 ], resolving[ 1 ] };
		pending.resolveOrReject( true, JS_DupValue( realm.get(), value.get() ) );
		CHECK(
			JS_PromiseState( realm.get(), promise.get() ) == ( timeout ? JS_PROMISE_REJECTED : JS_PROMISE_FULFILLED ) );
	}
	else if ( host_reentry )
	{
		const downloader::ScriptContext::ExecutionScope run { realm.get() };
		const downloader::ScriptContext::HostCall host;
		std::this_thread::sleep_for( std::chrono::milliseconds { 20 } );
		CHECK_FALSE( activity->snapshot().executing );
		(void)downloader::scriptValueString( realm.get(), value.get() );
		CHECK_FALSE( activity->snapshot().executing );
		std::this_thread::sleep_for( std::chrono::milliseconds { 20 } );
	}
	else if ( phase == "json" )
	{
		const auto converted_e { downloader::scriptValueToJson( realm.get(), value.get() ) };
		(void)converted_e;
	}
	else if ( phase == "error" )
		(void)downloader::scriptErrorString( realm.get(), value.get() );
	else
		(void)downloader::scriptValueString( realm.get(), value.get() );

	CHECK_FALSE( activity->snapshot().executing );
	CHECK( activity->snapshot().busy_ns - before >= ( timeout ? 90000000 : 18000000 ) );
	const auto log { output.str() };
	const auto yield { log.find( "downloader script timing:" ) };
	REQUIRE( yield != std::string::npos );
	const auto record { log.substr( yield ) };
	CHECK( timingField( record, "run" ) >= ( timeout ? 90000 : 18000 ) );
}
} // namespace idhan::test
