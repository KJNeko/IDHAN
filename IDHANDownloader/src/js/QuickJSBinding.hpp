#pragma once

#include <cstddef>
#include <cstdint>
#include <exception>
#include <new>
#include <quickjs.h>

#define IDHAN_QJS_FUNCTION( name )                                                                                     \
	JSValue name( JSContext* context, JSValueConst this_value, int argument_count, JSValueConst* arguments )

#define IDHAN_QJS_CONSTRUCTOR( name )                                                                                  \
	JSValue name( JSContext* context, JSValueConst new_target, int argument_count, JSValueConst* arguments )

#define IDHAN_QJS_GETTER( name ) JSValue name( JSContext* context, JSValueConst this_value )

#define IDHAN_QJS_SETTER( name ) JSValue name( JSContext* context, JSValueConst this_value, JSValueConst value )

#define IDHAN_QJS_METHOD( javascript_name, argument_count, function )                                                  \
	::idhan::downloader::quickjs::method( javascript_name, argument_count, function )

#define IDHAN_QJS_PROPERTY( javascript_name, getter, setter )                                                          \
	::idhan::downloader::quickjs::property( javascript_name, getter, setter )

#define IDHAN_QJS_READONLY_PROPERTY( javascript_name, getter )                                                         \
	::idhan::downloader::quickjs::property( javascript_name, getter, nullptr )

#define IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )                                                                      \
	catch ( const std::bad_alloc& )                                                                                    \
	{                                                                                                                  \
		return JS_ThrowOutOfMemory( context );                                                                         \
	}                                                                                                                  \
	catch ( const std::exception& exception )                                                                          \
	{                                                                                                                  \
		return JS_ThrowInternalError( context, "%s", exception.what() );                                               \
	}                                                                                                                  \
	catch ( ... )                                                                                                      \
	{                                                                                                                  \
		return JS_ThrowInternalError( context, "Unknown native exception" );                                           \
	}

namespace idhan::downloader::quickjs
{
using Getter = JSValue ( * )( JSContext*, JSValueConst );
using Setter = JSValue ( * )( JSContext*, JSValueConst, JSValueConst );

inline JSCFunctionListEntry method( const char* name, const std::uint8_t length, JSCFunction* function )
{
	JSCFunctionListEntry entry {};
	entry.name = name;
	entry.prop_flags = JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE;
	entry.def_type = JS_DEF_CFUNC;
	entry.u.func.length = length;
	entry.u.func.cproto = JS_CFUNC_generic;
	entry.u.func.cfunc.generic = function;
	return entry;
}

inline JSCFunctionListEntry property( const char* name, Getter getter, Setter setter )
{
	JSCFunctionListEntry entry {};
	entry.name = name;
	entry.prop_flags = JS_PROP_CONFIGURABLE;
	entry.def_type = JS_DEF_CGETSET;
	entry.u.getset.get.getter = getter;
	entry.u.getset.set.setter = setter;
	return entry;
}

template < std::size_t EntryCount >
bool installClass(
	JSContext* context,
	JSValueConst global,
	JSClassID& class_id,
	const JSClassDef& definition,
	JSCFunction* constructor,
	const int constructor_arguments,
	const JSCFunctionListEntry ( &prototype_entries )[ EntryCount ] )
{
	if ( class_id == 0 ) JS_NewClassID( &class_id );

	JSRuntime* runtime { JS_GetRuntime( context ) };

	if ( !JS_IsRegisteredClass( runtime, class_id ) && JS_NewClass( runtime, class_id, &definition ) < 0 ) return false;

	JSValue prototype { JS_NewObject( context ) };

	if ( JS_IsException( prototype ) ) return false;

	if ( JS_SetPropertyFunctionList( context, prototype, prototype_entries, static_cast< int >( EntryCount ) ) < 0 )
	{
		JS_FreeValue( context, prototype );
		return false;
	}

	JSValue constructor_value {
		JS_NewCFunction2( context, constructor, definition.class_name, constructor_arguments, JS_CFUNC_constructor, 0 )
	};

	if ( JS_IsException( constructor_value ) )
	{
		JS_FreeValue( context, prototype );
		return false;
	}

	if ( JS_SetConstructor( context, constructor_value, prototype ) < 0 )
	{
		JS_FreeValue( context, constructor_value );
		JS_FreeValue( context, prototype );
		return false;
	}

	JS_SetClassProto( context, class_id, prototype );
	return JS_SetPropertyStr( context, global, definition.class_name, constructor_value ) >= 0;
}
} // namespace idhan::downloader::quickjs
