#include "js/ScriptContext.hpp"

#include <spdlog/spdlog.h>

#include <cstring>
#include <utility>

#include "js/ScriptExecution.hpp"
#include "js/bindings/IdhanBindings.hpp"
#include "logging/format_ns.hpp"
#include "scripts/BytecodeCache.hpp"

namespace idhan::downloader
{

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
	const auto resolved { self->m_bytecode.resolve( name, std::filesystem::path { base == nullptr ? "" : base } ) };

	if ( !resolved )
	{
		spdlog::warn( "downloader script: {}", resolved.error() );
		return nullptr;
	}

	const std::string text { resolved->string() };
	auto* output { static_cast< char* >( js_malloc( context, text.size() + 1 ) ) };

	if ( output == nullptr ) return nullptr;

	std::memcpy( output, text.c_str(), text.size() + 1 );
	return output;
}

JSModuleDef* ScriptContext::loadModule( JSContext* context, const char* name, void* opaque )
{
	auto* self { static_cast< ScriptContext* >( opaque ) };
	auto bytecode { self->m_bytecode.bytecode( std::filesystem::path { name } ) };

	if ( !bytecode )
	{
		JS_ThrowReferenceError( context, "%s", bytecode.error().c_str() );
		return nullptr;
	}

	JSValue value { JS_ReadObject( context, bytecode->data(), bytecode->size(), JS_READ_OBJ_BYTECODE ) };

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

ScriptContext::ScriptContext( Options options, BytecodeCache& bytecode ) :
  m_options( options ),
  m_bytecode( bytecode ),
  m_runtime( JS_NewRuntime() )
{
	if ( m_runtime == nullptr ) return;

	JS_SetMemoryLimit( m_runtime.get(), m_options.memory_limit );
	JS_SetMaxStackSize( m_runtime.get(), m_options.stack_limit );
	JS_SetCanBlock( m_runtime.get(), false );
	JS_SetInterruptHandler( m_runtime.get(), interrupt, &m_burst );
	JS_SetHostPromiseRejectionTracker( m_runtime.get(), reportRejection, this );
	JS_SetModuleLoaderFunc( m_runtime.get(), normalizeModule, loadModule, this );
}

ScriptContext::~ScriptContext() = default;

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
	if ( context != nullptr ) JS_FreeContext( context );
}

std::expected< JSValue, std::string > ScriptContext::evaluate(
	ScriptExecution& execution,
	const std::filesystem::path& script )
{
	auto bytecode { m_bytecode.bytecode( script ) };

	if ( !bytecode ) return std::unexpected( bytecode.error() );

	JSContext* context { execution.context };
	JSValue value { JS_ReadObject( context, bytecode->data(), bytecode->size(), JS_READ_OBJ_BYTECODE ) };

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

	enterBurst();
	JSValue evaluated { JS_EvalFunction( context, value ) };
	leaveBurst();

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

void ScriptContext::enterBurst()
{
	m_burst.deadline = std::chrono::steady_clock::now() + m_options.burst_timeout;
	m_burst.armed = true;
}

void ScriptContext::leaveBurst()
{
	m_burst.armed = false;
}

bool ScriptContext::pumpJobs( std::string& error )
{
	enterBurst();

	while ( true )
	{
		JSContext* job_context {};
		const int executed { JS_ExecutePendingJob( m_runtime.get(), &job_context ) };

		if ( executed == 0 ) break;

		if ( executed < 0 )
		{
			error = scriptExceptionString( job_context );
			leaveBurst();
			return false;
		}
	}

	leaveBurst();
	return true;
}

void ScriptContext::collectGarbage()
{
	if ( m_runtime != nullptr ) JS_RunGC( m_runtime.get() );
}

} // namespace idhan::downloader
