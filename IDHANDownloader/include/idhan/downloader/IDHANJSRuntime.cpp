#include "IDHANJSRuntime.hpp"

#include <new>

#include "JSReturn.hpp"

IDHANJSRuntime::IDHANJSRuntime() : m_runtime( JS_NewRuntime() )
{
	if ( !m_runtime )
	{
		throw std::bad_alloc {};
	}

	m_context.reset( JS_NewContext( m_runtime.get() ) );

	if ( !m_context )
	{
		throw std::bad_alloc {};
	}
}

JSContext* IDHANJSRuntime::context() const noexcept
{
	return m_context.get();
}

JSRuntime* IDHANJSRuntime::runtime() const noexcept
{
	return m_runtime.get();
}

JSReturn IDHANJSRuntime::newObject()
{
	JSValue result { JS_NewObject( context() ) };
	if ( JS_IsException( result ) )
	{
		throw std::runtime_error( "Failed to create new JS object" );
	}
	return { shared_from_this(), result };
}

bool IDHANJSRuntime::isRegisteredClass( const JSClassID class_id ) const
{
	return JS_IsRegisteredClass( runtime(), class_id );
}

int IDHANJSRuntime::newClass( const JSClassID class_id, const JSClassDef* definition ) const
{
	return JS_NewClass( runtime(), class_id, definition );
}

void IDHANJSRuntime::setClassProto( const JSClassID class_id, const JSReturn& proto ) const
{
	return setClassProto( class_id, proto.dupValue() );
}

void IDHANJSRuntime::setClassProto( const JSClassID class_id, const JSValue proto ) const
{
	return JS_SetClassProto( context(), class_id, proto );
}

JSReturn IDHANJSRuntime::createConstructor( const char* name, JSCFunction* callback, const int arg_count )
{
	JSValue constructor { JS_NewCFunction2( context(), callback, name, arg_count, JS_CFUNC_constructor, 0 ) };

	if ( JS_IsException( constructor ) ) throw std::runtime_error( "Failed to create new JS constructor" );

	return { shared_from_this(), constructor };
}

JSReturn IDHANJSRuntime::createFunction( const char* name, JSCFunction* callback, int argument_count )
{
	JSValue function { JS_NewCFunction( context(), callback, name, argument_count ) };

	if ( JS_IsException( function ) ) throw std::runtime_error( "Failed to create new JS function" );

	return JSReturn { shared_from_this(), function };
}

JSReturn IDHANJSRuntime::getGlobalObject() noexcept
{
	JSValue global { JS_GetGlobalObject( context() ) };

	if ( JS_IsException( global ) )
	{
		throw std::runtime_error( "Failed to get global object" );
	}

	return { shared_from_this(), global };
}

int IDHANJSRuntime::setConstructor( const JSReturn& constructor, const JSReturn& proto ) const
{
	return JS_SetConstructor( context(), constructor.m_value, proto.m_value );
}
