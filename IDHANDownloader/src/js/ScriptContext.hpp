#pragma once

#include <json/value.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <map>
#include <limits>
#include <memory>
#include <quickjs.h>
#include <string>
#include <string_view>
#include <vector>

#include "js/QuickJSHandles.hpp"
#include "scripts/ParserKeyValueStore.hpp"

namespace idhan::downloader
{
class BytecodeCache;
class ScriptActivity;
struct ScriptExecution;

class ScriptContext
{
  public:

	struct Options
	{
		std::size_t memory_limit { 256 * 1024 * 1024 };
		std::size_t stack_limit { 1024 * 1024 };
		std::chrono::milliseconds burst_timeout { 5000 };
	};

	// Excludes synchronous host callbacks from the JavaScript budget and run timing.
	// Nested callbacks share the outer pause; callbacks on other threads have no active burst.
	class HostCall
	{
		ScriptContext* m_context {};

	  public:

		HostCall();
		HostCall( const HostCall& ) = delete;
		HostCall& operator=( const HostCall& ) = delete;
		~HostCall();
	};

	// Nesting shares the outer timer and watchdog. A scope inside a host callback
	// temporarily resumes script accounting so getters cannot hide in host wait time.
	class ExecutionScope
	{
		ScriptContext* m_context {};
		bool m_resume_host {};

	  public:

		explicit ExecutionScope( JSContext* context );
		ExecutionScope( ScriptContext& context, std::string parser );
		ExecutionScope( const ExecutionScope& ) = delete;
		ExecutionScope& operator=( const ExecutionScope& ) = delete;
		~ExecutionScope();
		void finish();
	};

  private:

	struct Burst
	{
		std::chrono::steady_clock::time_point deadline {};
		std::chrono::steady_clock::time_point started {};
		std::chrono::steady_clock::time_point host_started {};
		std::chrono::nanoseconds host_time {};
		std::string parser {};
		bool armed {};
		std::size_t depth {};
		ScriptContext* previous_active {};
	};

	std::shared_ptr< ParserKeyValueStore > m_key_values;
	Options m_options;
	BytecodeCache& m_bytecode;
	Burst m_burst {};
	JSRuntimePtr m_runtime {};
	struct ParserState;
	std::vector< std::unique_ptr< ParserState > > m_parsers {};
	std::shared_ptr< ScriptActivity > m_activity {};
	std::map< std::string, std::chrono::nanoseconds > m_timings {};
	std::size_t m_gc_releases {};
	// Keep the watchdog budget across scheduler yields until the microtask queue drains.
	std::chrono::nanoseconds m_job_run {};
	static thread_local ScriptContext* s_active;

	static int interrupt( JSRuntime* runtime, void* opaque );
	static char* normalizeModule( JSContext* context, const char* base, const char* name, void* opaque );
	static JSModuleDef* loadModule( JSContext* context, const char* name, void* opaque );
	static std::string timingParser( JSContext* context );
	void recordTiming( const std::string& parser, std::chrono::nanoseconds run );
	void enterBurstForParser( std::string parser );
	void pauseForHost();
	void resumeFromHost();

  public:

	ScriptContext(
		Options options,
		BytecodeCache& bytecode,
		std::shared_ptr< ScriptActivity > activity = {},
		std::shared_ptr< ParserKeyValueStore > key_values = std::make_shared< ParserKeyValueStore >() );
	ScriptContext( const ScriptContext& ) = delete;
	ScriptContext& operator=( const ScriptContext& ) = delete;
	~ScriptContext();

	[[nodiscard]] ParserKeyValueStore& keyValues() { return *m_key_values; }

	[[nodiscard]] bool valid() const { return m_runtime != nullptr; }

	[[nodiscard]] JSRuntime* runtime() const { return m_runtime.get(); }

	[[nodiscard]] std::expected< JSContext*, std::string > createRealm( ScriptExecution& execution );
	void freeRealm( JSContext* context );
	// Retain each registry/secrets configuration independently while its invocations finish.
	[[nodiscard]] JSValue parserState(
		ScriptExecution& execution,
		std::string parser,
		std::string origin,
		Json::Value secrets );

	[[nodiscard]] std::expected< JSValue, std::string > evaluate(
		ScriptExecution& execution,
		const std::filesystem::path& script );

	void enterBurst( JSContext* context = nullptr );
	void leaveBurst();

	// A scheduler yields between jobs; a synchronous job retains the burst watchdog.
	bool runReadyMicrotasks(
		std::string& error,
		std::size_t max_jobs = std::numeric_limits< std::size_t >::max(),
		std::chrono::milliseconds budget = std::chrono::milliseconds::max() );

	void collectGarbage();
};

} // namespace idhan::downloader
