#pragma once

#include <chrono>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <quickjs.h>
#include <string>

#include "js/QuickJSHandles.hpp"

namespace idhan::downloader
{
class BytecodeCache;
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

	// Excludes synchronous host callbacks from the active worker's JavaScript budget.
	// Nested callbacks share the outer pause; callbacks on other threads have no active burst.
	class HostCall
	{
		ScriptContext* m_context {};
		std::chrono::steady_clock::time_point m_started {};

	  public:

		HostCall();
		HostCall( const HostCall& ) = delete;
		HostCall& operator=( const HostCall& ) = delete;
		~HostCall();
	};

  private:

	struct Burst
	{
		std::chrono::steady_clock::time_point deadline {};
		bool armed {};
	};

	Options m_options;
	BytecodeCache& m_bytecode;
	Burst m_burst {};
	JSRuntimePtr m_runtime {};
	static thread_local ScriptContext* s_active;

	static int interrupt( JSRuntime* runtime, void* opaque );
	static char* normalizeModule( JSContext* context, const char* base, const char* name, void* opaque );
	static JSModuleDef* loadModule( JSContext* context, const char* name, void* opaque );

  public:

	ScriptContext( Options options, BytecodeCache& bytecode );
	ScriptContext( const ScriptContext& ) = delete;
	ScriptContext& operator=( const ScriptContext& ) = delete;
	~ScriptContext();

	[[nodiscard]] bool valid() const { return m_runtime != nullptr; }

	[[nodiscard]] JSRuntime* runtime() const { return m_runtime.get(); }

	[[nodiscard]] std::expected< JSContext*, std::string > createRealm( ScriptExecution& execution );
	void freeRealm( JSContext* context );

	[[nodiscard]] std::expected< JSValue, std::string > evaluate(
		ScriptExecution& execution,
		const std::filesystem::path& script );

	void enterBurst();
	void leaveBurst();

	bool pumpJobs( std::string& error );

	void collectGarbage();
};

} // namespace idhan::downloader
