#include "JSReturn.hpp"

#include <stdexcept>
#include <string>
#include <utility>

#include "IDHANJSRuntime.hpp"

class JSStringBuffer
{
	JSContext* m_context;
	const char* m_buffer;

  public:

	JSStringBuffer( JSContext* context, const char* buffer ) : m_context { context }, m_buffer { buffer } {}

	JSStringBuffer( const JSStringBuffer& ) = delete;
	JSStringBuffer& operator=( const JSStringBuffer& ) = delete;

	~JSStringBuffer()
	{
		if ( m_buffer ) JS_FreeCString( m_context, m_buffer );
	}

	[[nodiscard]] const char* get() const { return m_buffer; }
};

JSReturn::JSReturn( std::shared_ptr< IDHANJSRuntime > ctx ) : m_ctx( std::move( ctx ) )
{}

JSReturn::JSReturn( std::shared_ptr< IDHANJSRuntime > ctx, const JSValue value ) :
  m_ctx( std::move( ctx ) ),
  m_value( value )
{}

JSReturn::JSReturn( JSReturn&& other ) noexcept :
  m_ctx( std::move( other.m_ctx ) ),
  m_value( std::exchange( other.m_value, JS_UNDEFINED ) )
{}

JSReturn& JSReturn::operator=( JSReturn&& other ) noexcept
{
	if ( this == &other )
	{
		return *this;
	}

	if ( !JS_IsUndefined( m_value ) )
	{
		JS_FreeValue( m_ctx->context(), m_value );
	}

	m_ctx = std::move( other.m_ctx );
	m_value = std::exchange( other.m_value, JS_UNDEFINED );
	return *this;
}

bool JSReturn::isException() const
{
	return JS_IsException( m_value );
}

bool JSReturn::isUndefined() const
{
	return JS_IsUndefined( m_value );
}

bool JSReturn::isNull() const
{
	return JS_IsNull( m_value );
}

bool JSReturn::isObject() const
{
	return JS_IsObject( m_value );
}

bool JSReturn::isString() const
{
	return JS_IsString( m_value );
}

bool JSReturn::isArray() const
{
	if ( !m_ctx )
	{
		throw std::runtime_error( "Array check requires a JavaScript context" );
	}

	const auto result { JS_IsArray( m_ctx->context(), m_value ) };
	if ( result < 0 )
	{
		throw std::runtime_error( "Failed to check JavaScript array" );
	}

	return result != 0;
}

bool JSReturn::isFunction() const
{
	if ( !m_ctx )
	{
		throw std::runtime_error( "Function check requires a JavaScript context" );
	}

	return JS_IsFunction( m_ctx->context(), m_value );
}

JSReturn JSReturn::stringifyJson() const
{
	if ( !m_ctx )
	{
		throw std::runtime_error( "JSON serialization requires a JavaScript context" );
	}

	return JSReturn { m_ctx, JS_JSONStringify( m_ctx->context(), m_value, JS_UNDEFINED, JS_UNDEFINED ) };
}

std::string JSReturn::toString() const
{
	if ( !m_ctx )
	{
		throw std::runtime_error( "String conversion requires a JavaScript context" );
	}

	auto* ctx { m_ctx->context() };
	std::size_t size {};
	JSStringBuffer buffer { ctx, JS_ToCStringLen( ctx, &size, m_value ) };

	if ( !buffer.get() )
	{
		throw std::runtime_error( "Failed to convert JavaScript value to string" );
	}

	return std::string { buffer.get(), size };
}

JSReturn JSReturn::getProperty( const char* name ) const
{
	if ( !m_ctx )
	{
		throw std::runtime_error( "Property access requires a JavaScript context" );
	}

	if ( name == nullptr )
	{
		throw std::invalid_argument( "Property name must not be null" );
	}

	JSReturn property { m_ctx, JS_GetPropertyStr( m_ctx->context(), m_value, name ) };

	if ( property.isException() )
	{
		throw std::runtime_error( std::string( "Failed to read JavaScript property: " ) + name );
	}

	return property;
}

int JSReturn::setProperty( const char* name, const JSReturn& value ) const
{
	return JS_SetPropertyStr( m_ctx->context(), m_value, name, value.dupValue() );
}

JSValue JSReturn::dupValue() const
{
	return JS_DupValue( m_ctx->context(), m_value );
}

JSReturn::~JSReturn()
{
	if ( !JS_IsUndefined( m_value ) ) JS_FreeValue( m_ctx->context(), m_value );
}
