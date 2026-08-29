#pragma once

#include <cstdint>
#include <memory>
#include <quickjs.h>

namespace idhan::downloader
{

struct JSRuntimeDeleter
{
	void operator()( JSRuntime* runtime ) const noexcept { JS_FreeRuntime( runtime ); }
};

struct JSContextDeleter
{
	void operator()( JSContext* context ) const noexcept { JS_FreeContext( context ); }
};

struct JSAllocationDeleter
{
	JSContext* context {};

	void operator()( void* allocation ) const noexcept { js_free( context, allocation ); }
};

using JSRuntimePtr = std::unique_ptr< JSRuntime, JSRuntimeDeleter >;
using JSContextPtr = std::unique_ptr< JSContext, JSContextDeleter >;
using JSBufferPtr = std::unique_ptr< std::uint8_t, JSAllocationDeleter >;

} // namespace idhan::downloader
