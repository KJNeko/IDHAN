#pragma once

#include <memory>

#include "quickjs.h"

class IDHANJSRuntime
{
	struct RuntimeDeleter
	{
		void operator()( JSRuntime* ptr ) const noexcept { JS_FreeRuntime( ptr ); }
	};

	struct ContextDeleter
	{
		void operator()( JSContext* ptr ) const noexcept
		{
			if ( ptr )
			{
				JS_FreeContext( ptr );
			}
		}
	};

	// Owns memory and GC
	std::unique_ptr< JSRuntime, RuntimeDeleter > m_runtime;
	// Globals and execution
	std::unique_ptr< JSContext, ContextDeleter > m_context;

  public:

	IDHANJSRuntime();

	[[nodiscard]] JSContext* context() const noexcept;
};
