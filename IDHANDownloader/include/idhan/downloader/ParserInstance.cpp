#include "ParserInstance.hpp"

#include <spdlog/spdlog.h>

#include <stdexcept>
#include <utility>

#include "bindings/installBindings.hpp"

std::string JSModule::errorMessage( const char* operation, const JSReturn& exception ) const
{
	std::string message { std::string( operation ) + " [" + m_filename + "]: " };

	try
	{
		message += exception.toString();
	}
	catch ( const std::runtime_error& )
	{
		message += "Unknown JavaScript error";
	}

	if ( exception.isObject() )
	{
		try
		{
			const auto stack { exception.getProperty( "stack" ) };
			if ( stack.isString() )
			{
				message += "\n";
				message += stack.toString();
			}
		}
		catch ( const std::runtime_error& )
		{
			// Preserve the original error if its stack cannot be read.
		}
	}

	return message;
}

void JSModule::resolveImports() const
{
	spdlog::trace( "Resolving imports for {}", m_filename );
	if ( JS_ResolveModule( m_module.m_ctx->context(), m_module.m_value ) < 0 )
	{
		throw std::runtime_error( errorMessage(
			"Failed to resolve module", JSReturn { m_module.m_ctx, JS_GetException( m_module.m_ctx->context() ) } ) );
	}
}

void JSModule::evaluate() const
{
	spdlog::trace( "Evaluating module {}", m_filename );
	auto* ctx { m_module.m_ctx->context() };
	JSReturn evaluation { m_module.m_ctx, JS_EvalFunction( ctx, JS_DupValue( ctx, m_module.m_value ) ) };

	if ( evaluation.isException() )
	{
		throw std::runtime_error( errorMessage(
			"Failed to evaluate module", JSReturn { m_module.m_ctx, JS_GetException( m_module.m_ctx->context() ) } ) );
	}

	const auto state { JS_PromiseState( ctx, evaluation.m_value ) };
	if ( state == JS_PROMISE_REJECTED )
	{
		throw std::runtime_error( errorMessage(
			"Module initialization rejected",
			JSReturn { m_module.m_ctx, JS_PromiseResult( ctx, evaluation.m_value ) } ) );
	}

	if ( state == JS_PROMISE_PENDING )
	{
		throw std::runtime_error( "Asynchronous module initialization is unsupported" );
	}
}

void JSModule::loadNamespace()
{
	spdlog::trace( "Loading module namespace for {}", m_filename );
	auto* ctx { m_module.m_ctx->context() };
	auto* definition { static_cast< JSModuleDef* >( JS_VALUE_GET_PTR( m_module.m_value ) ) };
	m_namespace = JSReturn { m_module.m_ctx, JS_GetModuleNamespace( ctx, definition ) };

	if ( m_namespace.isException() )
	{
		throw std::runtime_error( errorMessage(
			"Failed to get module namespace",
			JSReturn { m_module.m_ctx, JS_GetException( m_module.m_ctx->context() ) } ) );
	}
}

JSModule::JSModule( JSReturn module, std::string filename ) :
  m_module( std::move( module ) ),
  m_filename( std::move( filename ) )
{
	if ( !m_module.m_ctx )
	{
		throw std::runtime_error( "Module requires a runtime" );
	}

	if ( JS_VALUE_GET_TAG( m_module.m_value ) != JS_TAG_MODULE )
	{
		throw std::runtime_error( "Expected a compiled JavaScript module" );
	}

	resolveImports();

	evaluate();
	loadNamespace();
	spdlog::debug( "Initialized parser module {}", m_filename );
}

JSReturn JSModule::getExport( const char* name ) const
{
	auto value { findExport( name ) };
	if ( value.isUndefined() )
	{
		throw std::runtime_error( "Missing module export" );
	}

	return value;
}

JSReturn JSModule::findExport( const char* name ) const
{
	JSReturn value { m_module.m_ctx, JS_GetPropertyStr( m_module.m_ctx->context(), m_namespace.m_value, name ) };

	if ( value.isException() )
	{
		throw std::runtime_error( errorMessage(
			"Failed to read module export",
			JSReturn { m_module.m_ctx, JS_GetException( m_module.m_ctx->context() ) } ) );
	}

	return value;
}

JSReturn ParserInstance::loadBytecode() const
{
	spdlog::trace( "Loading parser bytecode for {}", m_script->filename() );
	const auto bytecode { m_script->bytecode() };

	JSReturn ret { m_runtime,
	               JS_ReadObject( m_runtime->context(), bytecode.data(), bytecode.size(), JS_READ_OBJ_BYTECODE ) };

	if ( ret.isException() )
	{
		throw std::runtime_error( "Failed to load compiled parser bytecode" );
	}

	return ret;
}

ParserInstance::ParserInstance( const std::shared_ptr< CompiledScript >& script ) :
  m_script( script ),
  m_runtime( idhan::js::installBindings( std::make_shared< IDHANJSRuntime >() ) ),
  m_module( loadBytecode(), m_script->filename() )
{}

ParserManifest ParserInstance::manifest()
{
	const auto parser_class { m_module.getExport( "default" ) };

	if ( !parser_class.isFunction() )
	{
		throw std::runtime_error( "Expected a default-exported parser class" );
	}

	const auto metadata { parser_class.getProperty( "metadata" ) };

	if ( metadata.isUndefined() || metadata.isNull() )
	{
		throw std::runtime_error( "Parser manifest is missing: expected default.metadata" );
	}

	if ( !metadata.isObject() || metadata.isArray() )
	{
		throw std::runtime_error( "Parser manifest must be an object" );
	}

	JSReturn json { metadata.stringifyJson() };

	if ( json.isException() || !json.isString() )
	{
		throw std::runtime_error( "Failed to serialize parser manifest" );
	}

	const auto text { json.toString() };
	simdjson::dom::parser parser {};
	simdjson::dom::element root {};
	const auto error { parser.parse( text ).get( root ) };
	if ( error )
	{
		throw std::runtime_error( simdjson::error_message( error ) );
	}

	return ParserManifest { root };
}

std::vector< URLClass > ParserInstance::urlClasses() const
{
	const auto classes { m_module.getExport( "urlClasses" ) };
	if ( !classes.isArray() )
	{
		throw std::runtime_error( "urlClasses export must be an array" );
	}

	const auto json { classes.stringifyJson() };
	if ( json.isException() || !json.isString() )
	{
		throw std::runtime_error( "Failed to serialize URL classes" );
	}

	const auto text { json.toString() };
	simdjson::dom::parser parser {};
	simdjson::dom::element root {};
	const auto error { parser.parse( text ).get( root ) };
	if ( error )
	{
		throw std::runtime_error( simdjson::error_message( error ) );
	}

	simdjson::dom::array values {};
	if ( root.get_array().get( values ) )
	{
		throw std::runtime_error( "URL classes must be an array" );
	}

	std::vector< URLClass > result {};
	for ( const auto value : values )
	{
		result.emplace_back( value );
	}

	return result;
}

bool ParserInstance::hasManifest() const
{
	const auto parserClass { m_module.findExport( "default" ) };
	if ( parserClass.isUndefined() || parserClass.isNull() )
	{
		return false;
	}

	const auto metadata { parserClass.getProperty( "metadata" ) };
	return !metadata.isUndefined() && !metadata.isNull();
}

bool ParserInstance::hasUrlClass() const
{
	const auto urlClasses { m_module.findExport( "urlClasses" ) };
	return !urlClasses.isUndefined() && !urlClasses.isNull();
}
