#include "IDHANJSRuntime.hpp"

#include <new>

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
