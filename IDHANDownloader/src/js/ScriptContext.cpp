#include "js/ScriptContext.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstring>
#include <utility>

#include "js/ScriptExecution.hpp"
#include "js/ScriptActivity.hpp"
#include "js/ScriptTiming.hpp"
#include "js/bindings/IdhanBindings.hpp"
#include "logging/format_ns.hpp"
#include "scripts/BytecodeCache.hpp"

namespace idhan::downloader
{

BytecodeCache& executionBytecode( JSContext* context, BytecodeCache& fallback )
{
	const auto* execution { executionFor( context ) };

	if ( execution != nullptr && execution->route.bytecode != nullptr ) return *execution->route.bytecode;

	return fallback;
}

std::expected< std::span< const std::uint8_t >, std::string > readExecutionBytecode(
	JSContext* context,
	BytecodeCache& fallback,
	const std::filesystem::path& script )
{
	auto& cache { executionBytecode( context, fallback ) };
	const auto* execution { executionFor( context ) };
	if ( execution != nullptr && execution->route.bytecode != nullptr ) return cache.preparedBytecode( script );

	return cache.bytecode( script );
}

std::expected< std::filesystem::path, std::string > resolveExecutionModule(
	JSContext* context,
	BytecodeCache& fallback,
	const std::string_view specifier,
	const std::filesystem::path& referrer )
{
	auto& cache { executionBytecode( context, fallback ) };
	const auto* execution { executionFor( context ) };
	if ( execution != nullptr && execution->route.bytecode != nullptr )
		return cache.resolvePrepared( specifier, referrer );

	return cache.resolve( specifier, referrer );
}

thread_local ScriptContext* ScriptContext::s_active {};

struct ScriptContext::ParserState
{
	std::string parser {};
	std::string origin {};
	Json::Value secrets {};
	// Retained methods can import modules after the creating invocation has retired.
	ScriptExecution loader {};
	JSContextPtr context {};
	JSValue value { JS_UNDEFINED };

	~ParserState() { JS_FreeValue( context.get(), value ); }
};

ScriptContext::HostCall::HostCall()
{
	if ( s_active == nullptr ) return;

	if ( !s_active->m_burst.armed ) return;

	m_context = s_active;
	m_context->pauseForHost();
}

ScriptContext::HostCall::~HostCall()
{
	if ( m_context == nullptr ) return;

	m_context->resumeFromHost();
}

void ScriptContext::pauseForHost()
{
	m_burst.host_started = std::chrono::steady_clock::now();
	m_burst.armed = false;
	if ( m_activity != nullptr ) m_activity->leave();
}

void ScriptContext::resumeFromHost()
{
	const auto elapsed { std::chrono::steady_clock::now() - m_burst.host_started };
	m_burst.deadline += elapsed;
	m_burst.host_time += std::chrono::duration_cast< std::chrono::nanoseconds >( elapsed );
	m_burst.armed = true;
	if ( m_activity != nullptr ) m_activity->resume();
}

ScriptContext::ExecutionScope::ExecutionScope( JSContext* context )
{
	if ( context == nullptr ) return;

	m_context = static_cast< ScriptContext* >( JS_GetRuntimeOpaque( JS_GetRuntime( context ) ) );
	if ( m_context == nullptr ) return;

	m_resume_host = m_context->m_burst.depth != 0 && !m_context->m_burst.armed;
	if ( m_resume_host ) m_context->resumeFromHost();

	m_context->enterBurst( context );
}

ScriptContext::ExecutionScope::ExecutionScope( ScriptContext& context, std::string parser ) : m_context( &context )
{
	m_resume_host = context.m_burst.depth != 0 && !context.m_burst.armed;
	if ( m_resume_host ) context.resumeFromHost();

	context.enterBurstForParser( std::move( parser ) );
}

ScriptContext::ExecutionScope::~ExecutionScope()
{
	finish();
}

void ScriptContext::ExecutionScope::finish()
{
	if ( m_context == nullptr ) return;

	m_context->leaveBurst();
	if ( m_resume_host ) m_context->pauseForHost();

	m_context = nullptr;
}

int ScriptContext::interrupt( JSRuntime*, void* opaque )
{
	const auto* burst { static_cast< Burst* >( opaque ) };

	return burst->armed && std::chrono::steady_clock::now() >= burst->deadline ? 1 : 0;
}

static void reportRejection( JSContext* context, JSValueConst, JSValueConst reason, const int is_handled, void* )
{
	if ( is_handled != 0 ) return;

	spdlog::warn( "downloader script: unhandled promise rejection: {}", scriptErrorString( context, reason ) );
}

char* ScriptContext::normalizeModule( JSContext* context, const char* base, const char* name, void* opaque )
{
	auto* self { static_cast< ScriptContext* >( opaque ) };
	const auto resolved_e {
		resolveExecutionModule( context, self->m_bytecode, name, std::filesystem::path { base == nullptr ? "" : base } )
	};

	if ( !resolved_e.has_value() )
	{
		JS_ThrowReferenceError( context, "%s", resolved_e.error().c_str() );
		return nullptr;
	}

	const std::string text { resolved_e.value().string() };
	auto* output { static_cast< char* >( js_malloc( context, text.size() + 1 ) ) };

	if ( output == nullptr ) return nullptr;

	std::memcpy( output, text.c_str(), text.size() + 1 );
	return output;
}

JSModuleDef* ScriptContext::loadModule( JSContext* context, const char* name, void* opaque )
{
	auto* self { static_cast< ScriptContext* >( opaque ) };
	const auto bytecode_e { readExecutionBytecode( context, self->m_bytecode, std::filesystem::path { name } ) };

	if ( !bytecode_e.has_value() )
	{
		JS_ThrowReferenceError( context, "%s", bytecode_e.error().c_str() );
		return nullptr;
	}

	const auto bytecode { bytecode_e.value() };
	JSValue value { JS_ReadObject( context, bytecode.data(), bytecode.size(), JS_READ_OBJ_BYTECODE ) };

	if ( JS_IsException( value ) )
	{
		spdlog::warn( "downloader script: reading {} failed: {}", name, scriptExceptionString( context ) );
		return nullptr;
	}

	if ( JS_VALUE_GET_TAG( value ) != JS_TAG_MODULE )
	{
		spdlog::warn( "downloader script: {} read back as tag {}", name, JS_VALUE_GET_TAG( value ) );
		JS_FreeValue( context, value );
		JS_ThrowReferenceError( context, "%s is not a module", name );
		return nullptr;
	}

	if ( JS_ResolveModule( context, value ) < 0 )
	{
		spdlog::warn( "downloader script: resolving {} failed: {}", name, scriptExceptionString( context ) );
		JS_FreeValue( context, value );
		return nullptr;
	}

	auto* definition { static_cast< JSModuleDef* >( JS_VALUE_GET_PTR( value ) ) };
	JS_FreeValue( context, value );
	return definition;
}

ScriptContext::ScriptContext(
	Options options,
	BytecodeCache& bytecode,
	std::shared_ptr< ScriptActivity > activity,
	std::shared_ptr< ParserKeyValueStore > key_values ) :
  m_key_values( std::move( key_values ) ),
  m_options( options ),
  m_bytecode( bytecode ),
  m_runtime( JS_NewRuntime() ),
  m_activity( std::move( activity ) )
{
	if ( m_runtime == nullptr ) return;

	JS_SetRuntimeOpaque( m_runtime.get(), this );
	JS_SetMemoryLimit( m_runtime.get(), m_options.memory_limit );
	JS_SetMaxStackSize( m_runtime.get(), m_options.stack_limit );
	JS_SetCanBlock( m_runtime.get(), false );
	JS_SetInterruptHandler( m_runtime.get(), interrupt, &m_burst );
	JS_SetHostPromiseRejectionTracker( m_runtime.get(), reportRejection, this );
	JS_SetModuleLoaderFunc( m_runtime.get(), normalizeModule, loadModule, this );
}

ScriptContext::~ScriptContext()
{
	m_parsers.clear();
	m_runtime.reset();
}

std::expected< JSContext*, std::string > ScriptContext::createRealm( ScriptExecution& execution )
{
	if ( m_runtime == nullptr ) return std::unexpected( "The script runtime failed to start" );

	JSContextPtr context { JS_NewContext( m_runtime.get() ) };

	if ( context == nullptr ) return std::unexpected( "Unable to create a script realm" );

	JS_SetContextOpaque( context.get(), &execution );

	if ( const auto installed { installIdhanBindings( context.get() ) }; !installed )
		return std::unexpected( installed.error() );

	return context.release();
}

void ScriptContext::freeRealm( JSContext* context )
{
	if ( context == nullptr ) return;

	for ( const auto& state : m_parsers )
	{
		if ( state->context.get() != context ) continue;

		JS_SetContextOpaque( context, &state->loader );
		break;
	}

	JS_FreeContext( context );
}

JSValue ScriptContext::parserState(
	ScriptExecution& execution,
	std::string parser,
	std::string origin,
	Json::Value secrets )
{
	for ( const auto& state : m_parsers )
	{
		if ( state->parser != parser ) continue;

		if ( state->origin != origin ) continue;

		if ( state->loader.route.bytecode != execution.route.bytecode ) continue;

		if ( state->secrets != secrets ) continue;

		return JS_DupValue( execution.context, state->value );
	}

	auto state { std::make_unique< ParserState >() };
	state->parser = std::move( parser );
	state->origin = std::move( origin );
	state->secrets = std::move( secrets );
	state->loader.route = execution.route;
	state->context.reset( JS_DupContext( execution.context ) );
	state->loader.context = state->context.get();
	state->value = JS_NewObject( execution.context );
	const JSValue value { JS_DupValue( execution.context, state->value ) };
	m_parsers.emplace_back( std::move( state ) );
	return value;
}

std::expected< JSValue, std::string > ScriptContext::evaluate(
	ScriptExecution& execution,
	const std::filesystem::path& script )
{
	const auto bytecode_e { readExecutionBytecode( execution.context, m_bytecode, script ) };

	if ( !bytecode_e.has_value() ) return std::unexpected( bytecode_e.error() );

	const auto bytecode { bytecode_e.value() };
	JSContext* context { execution.context };
	JSValue value { JS_ReadObject( context, bytecode.data(), bytecode.size(), JS_READ_OBJ_BYTECODE ) };

	if ( JS_IsException( value ) ) return std::unexpected( scriptExceptionString( context ) );

	if ( JS_VALUE_GET_TAG( value ) != JS_TAG_MODULE )
	{
		JS_FreeValue( context, value );
		return std::unexpected( format_ns::format( "{} is not a module", script.string() ) );
	}

	if ( JS_ResolveModule( context, value ) < 0 )
	{
		JS_FreeValue( context, value );
		return std::unexpected( scriptExceptionString( context ) );
	}

	execution.module = static_cast< JSModuleDef* >( JS_VALUE_GET_PTR( value ) );

	const ExecutionScope timing { *this, script.string() };
	JSValue evaluated { JS_EvalFunction( context, value ) };

	if ( JS_IsException( evaluated ) ) return std::unexpected( scriptExceptionString( context ) );

	if ( JS_PromiseState( context, evaluated ) == JS_PROMISE_REJECTED )
	{
		JSValue reason { JS_PromiseResult( context, evaluated ) };
		std::string error { scriptErrorString( context, reason ) };
		JS_FreeValue( context, reason );
		JS_FreeValue( context, evaluated );
		return std::unexpected( std::move( error ) );
	}

	return evaluated;
}

std::string ScriptContext::timingParser( JSContext* context )
{
	if ( context == nullptr ) return "<retired context>";

	const auto* execution { executionFor( context ) };
	if ( execution == nullptr || execution->route.script.empty() ) return "<javascript>";

	return execution->route.script.string();
}

void ScriptContext::recordTiming( const std::string& parser, const std::chrono::nanoseconds run )
{
	auto& total { m_timings[ parser ] };
	total += run;
	if ( !spdlog::should_log( spdlog::level::debug ) ) return;

	spdlog::debug(
		"downloader script timing: parser='{}' run={} total={}",
		parser,
		formatScriptDuration( run ),
		formatScriptDuration( total ) );
}

void ScriptContext::enterBurst( JSContext* context )
{
	if ( m_burst.depth != 0 )
	{
		++m_burst.depth;
		return;
	}

	enterBurstForParser( context == nullptr ? "<javascript>" : timingParser( context ) );
}

void ScriptContext::enterBurstForParser( std::string parser )
{
	if ( m_burst.depth++ != 0 ) return;

	m_burst.parser = std::move( parser );
	m_burst.host_time = {};
	if ( m_activity != nullptr ) m_activity->enter();

	m_burst.started = std::chrono::steady_clock::now();
	m_burst.deadline = m_burst.started + m_options.burst_timeout;
	m_burst.armed = true;
	m_burst.previous_active = s_active;
	s_active = this;
}

void ScriptContext::leaveBurst()
{
	if ( m_burst.depth == 0 ) return;

	if ( --m_burst.depth != 0 ) return;

	const auto elapsed {
		std::chrono::duration_cast< std::chrono::nanoseconds >( std::chrono::steady_clock::now() - m_burst.started )
	};
	if ( m_activity != nullptr ) m_activity->leave();

	m_burst.armed = false;
	s_active = m_burst.previous_active;
	if ( !m_burst.parser.empty() ) recordTiming( m_burst.parser, elapsed - m_burst.host_time );
}

bool ScriptContext::runReadyMicrotasks(
	std::string& error,
	const std::size_t max_jobs,
	const std::chrono::milliseconds budget )
{
	if ( !JS_IsJobPending( m_runtime.get() ) )
	{
		m_job_run = {};
		return true;
	}

	if ( max_jobs == 0 ) return true;

	ExecutionScope burst { *this, {} };
	const auto remaining { std::max( std::chrono::nanoseconds::zero(), m_options.burst_timeout - m_job_run ) };
	m_burst.deadline = std::min( m_burst.deadline, std::chrono::steady_clock::now() + remaining );

	int executed {};
	std::size_t jobs {};
	const auto batch_started { std::chrono::steady_clock::now() };
	const auto host_started { m_burst.host_time };
	std::map< std::string, std::chrono::nanoseconds > timings;

	while ( JS_IsJobPending( m_runtime.get() ) )
	{
		JSContext* job_context {};
		const auto host_before { m_burst.host_time };
		const auto started { std::chrono::steady_clock::now() };
		executed = JS_ExecutePendingJob( m_runtime.get(), &job_context );
		if ( executed < 0 )
		{
			error = job_context == nullptr ?
			            "JavaScript job failed after its context retired" :
			            scriptExceptionString( job_context );
		}

		const auto elapsed {
			std::chrono::duration_cast< std::chrono::nanoseconds >( std::chrono::steady_clock::now() - started )
		};
		if ( executed != 0 )
		{
			auto& timing { timings[ timingParser( job_context ) ] };
			const auto host { m_burst.host_time - host_before };
			timing += elapsed - host;
		}

		if ( executed < 0 ) break;

		if ( ++jobs >= max_jobs ) break;

		if ( budget != std::chrono::milliseconds::max() )
		{
			if ( std::chrono::steady_clock::now() - batch_started >= budget ) break;
		}
	}

	m_job_run += std::chrono::steady_clock::now() - batch_started - ( m_burst.host_time - host_started );
	if ( executed < 0 || !JS_IsJobPending( m_runtime.get() ) ) m_job_run = {};

	burst.finish();
	for ( const auto& [ parser, timing ] : timings ) recordTiming( parser, timing );

	return executed >= 0;
}

void ScriptContext::collectGarbage()
{
	if ( m_runtime == nullptr ) return;

	if ( ++m_gc_releases < 64 ) return;

	m_gc_releases = 0;
	JS_RunGC( m_runtime.get() );
}

} // namespace idhan::downloader
