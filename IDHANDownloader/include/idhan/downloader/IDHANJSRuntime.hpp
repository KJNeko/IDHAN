#pragma once

#include <memory>

#include "JSReturn.hpp"
#include "quickjs.h"

class IDHANJSRuntime : public std::enable_shared_from_this< IDHANJSRuntime >
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
	[[nodiscard]] JSRuntime* runtime() const noexcept;

	[[nodiscard]] JSReturn newObject();

	[[nodiscard]] bool isRegisteredClass( JSClassID class_id ) const;

	[[nodiscard]] int newClass( JSClassID class_id, const JSClassDef* definition ) const;
	void setClassProto( JSClassID class_id, const JSReturn& proto ) const;
	void setClassProto( JSClassID class_id, JSValue proto ) const;

	[[nodiscard]] JSReturn createConstructor( const char* name, JSCFunction* callback, int arg_count );

	[[nodiscard]] JSReturn createFunction( const char* name, JSCFunction* callback, int argument_count );

	[[nodiscard]] JSReturn getGlobalObject() noexcept;

	[[nodiscard]] int setConstructor( const JSReturn& constructor, const JSReturn& proto ) const;
};
