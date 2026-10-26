#pragma once

#include <quickjs.h>
#include <utility>

namespace idhan::downloader
{

// Must be destroyed before its owning context; release transfers ownership to QuickJS.
class JSValueHandle
{
	JSContext* m_context;
	JSValue m_value;

  public:

	JSValueHandle( JSContext* context, JSValue value ) : m_context( context ), m_value( value ) {}

	JSValueHandle( const JSValueHandle& ) = delete;
	JSValueHandle& operator=( const JSValueHandle& ) = delete;

	JSValueHandle( JSValueHandle&& other ) noexcept : m_context( other.m_context ), m_value( other.release() ) {}

	~JSValueHandle() { JS_FreeValue( m_context, m_value ); }

	[[nodiscard]] JSValue get() const { return m_value; }

	[[nodiscard]] JSValue release() { return std::exchange( m_value, JS_UNDEFINED ); }
};

} // namespace idhan::downloader
