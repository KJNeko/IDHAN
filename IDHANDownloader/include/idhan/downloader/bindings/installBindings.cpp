
#include "installBindings.hpp"

#include <spdlog/spdlog.h>

#include <map>
#include <meta>
#include <mutex>
#include <optional>

#include "conversions/convertTo.hpp"
#include "idhan/downloader/IDHANJSRuntime.hpp"
#include "quickjs.h"

namespace idhan::js
{

struct Method
{
	const char* m_name;
	std::size_t length;

	consteval explicit Method( std::string_view text ) :
	  m_name( std::define_static_string( text ) ),
	  length( text.size() )
	{}
};

class Headers
{
	std::map< std::string, std::string > values;

  public:

	[[= js::Method( "set" )]] void set( std::string name, std::string value )
	{
		values.insert_or_assign( name, value );
	}

	[[= js::Method( "get" )]] std::optional< std::string > get( const std::string& name ) const
	{
		return values.at( name );
	}
};

consteval auto getPublicMembers( std::meta::info info )
{
	return std::define_static_array( std::meta::members_of( info, std::meta::access_context::current() ) );
}

consteval auto getMethodAnnotations( std::meta::info info )
{
	return std::define_static_array( std::meta::annotations_of_with_type( info, ^^Method ) );
}

template < typename T >
struct BindingClassID
{
	inline static JSClassID class_id {};
	inline static std::once_flag registration {};
};

template < typename T >
void deallocJSObject( JSRuntime*, JSValue value ) noexcept
{
	auto* instance = static_cast< T* >( JS_GetOpaque( value, BindingClassID< T >::class_id ) );

	delete instance;
}

template < typename T >
JSValue allocJSObject( JSContext* ctx, JSValueConst new_target, int argument_count, JSValueConst* args )
{
	try
	{
		//TODO: Allow arguments
		auto instance = std::make_unique< T >();

		const JSValue proto { JS_GetPropertyStr( ctx, new_target, "prototype" ) };

		if ( JS_IsException( proto ) ) return JS_EXCEPTION;

		const JSValue object { JS_NewObjectProtoClass( ctx, proto, BindingClassID< T >::class_id ) };

		JS_FreeValue( ctx, proto );

		if ( JS_IsException( object ) ) return object;

		JS_SetOpaque( object, instance.release() );

		return object;
	}
	catch ( std::bad_alloc& )
	{
		return JS_ThrowOutOfMemory( ctx );
	}
	catch ( std::exception& error )
	{
		return JS_ThrowInternalError( ctx, "%s", error.what() );
	}
	catch ( ... )
	{
		return JS_ThrowInternalError( ctx, "Unknown error" );
	}
}

consteval std::meta::info returnTypeOf( const std::meta::info info )
{
	auto return_type { std::meta::return_type_of( info ) };

	return return_type;
}

consteval bool isOptionalReturnType( const std::meta::info info )
{
	return std::meta::has_template_arguments( info ) && std::meta::template_of( info ) == ^^std::optional;
}

template < typename T, std::meta::info member, std::size_t ParamIndex = 0, typename... Converted >
JSValue invokeMethodConverted(
	JSContext* ctx,
	T* instance,
	int argument_count,
	JSValueConst* arguments,
	Converted&&... converted )
{
	constexpr auto return_type { returnTypeOf( member ) };
	constexpr auto parameters { std::define_static_array( std::meta::parameters_of( member ) ) };
	constexpr auto required_count { parameters.size() };

	if ( argument_count < static_cast< int >( required_count ) )
		return JS_ThrowTypeError(
			ctx, "Expected at least %d args, got %d", static_cast< int >( required_count ), argument_count );

	if constexpr ( ParamIndex == parameters.size() )
	{
		if constexpr ( return_type == ^^void )
		{
			instance->[:member:]( std::forward< Converted >( converted )... );
			return JS_UNDEFINED;
		}
		else
		{
			auto ret { instance->[:member:]( std::forward< Converted >( converted )... ) };

			if constexpr ( isOptionalReturnType( return_type ) )
				return convertOptional( ctx, std::move( ret ) );
			else
				return convertFrom< typename[:return_type:] >( ctx, std::move( ret ) );
		}
	}
	else
	{
		// Grab argument type
		using Argument = std::remove_cvref_t< typename[:std::meta::type_of( parameters[ ParamIndex ] ):] >;

		// Grab argument or undefined if out of range (Earlier check should prevent this)
		const JSValueConst value { ParamIndex < argument_count ? arguments[ ParamIndex ] : JS_UNDEFINED };

		const auto argument_e { convertTo< Argument >( ctx, value ) };

		if ( !argument_e ) return JS_EXCEPTION;

		return invokeMethodConverted< T, member, ParamIndex + 1 >(
			ctx,
			instance,
			argument_count,
			arguments,
			std::forward< Converted >( converted )...,
			std::move( argument_e.value() ) );
	}
}

// Member callback
template < typename T, std::meta::info member >
JSValue invokeMethod( JSContext* ctx, JSValueConst this_value, int argument_count, JSValueConst* arguments )
{
	auto* instance { static_cast< T* >( JS_GetOpaque2( ctx, this_value, BindingClassID< T >::class_id ) ) };

	constexpr auto return_type { std::meta::return_type_of( member ) };
	constexpr auto parameters { std::define_static_array( std::meta::parameters_of( member ) ) };

	// if there is no arguments we can just call the member function
	if constexpr ( parameters.empty() )
	{
		if constexpr ( return_type == ^^void )
		{
			instance->[:member:]();
			return JS_UNDEFINED;
		}
		else
		{
			auto ret { instance->[:member:]() };
			if constexpr ( isOptionalReturnType( return_type ) )
				return convertOptional( ctx, std::move( ret ) );
			else
				return convertFrom< typename[:return_type:] >( ctx, std::move( ret ) );
		}
	}
	else
	{
		return invokeMethodConverted< T, member >( ctx, instance, argument_count, arguments );
	}
}

template < typename T >
void installBindingsFor( const std::shared_ptr< IDHANJSRuntime >& ctx )
{
	constexpr auto type_info { ^^T };
	std::call_once( BindingClassID< T >::registration, []() { JS_NewClassID( &BindingClassID< T >::class_id ); } );
	const auto class_name { std::define_static_string( std::meta::identifier_of( ^^T ) ) };

	const auto class_id { BindingClassID< T >::class_id };

	// Check if the class has been registered
	if ( !ctx->isRegisteredClass( class_id ) )
	{
		spdlog::info( "Registering class {} as ID {}", class_name, class_id );

		const JSClassDef definition { .class_name = class_name, .finalizer = deallocJSObject< T > };

		if ( ctx->newClass( class_id, &definition ) < 0 )
		{
			throw std::runtime_error( "Failed to register native JS class" );
		}
	}

	JSReturn proto { ctx->newObject() };

	template for ( constexpr auto member : getPublicMembers( type_info ) )
	{
		constexpr auto method_annotations { getMethodAnnotations( member ) };

		if constexpr ( !method_annotations.empty() )
		{
			static_assert( method_annotations.size() == 1, "Expected one member annotation, Got N > 1" );

			constexpr auto method { std::meta::extract< Method >( method_annotations[ 0 ] ) };

			static_assert( std::meta::is_function( member ), "Method must be on a function" );

			constexpr auto parameters { std::define_static_array( std::meta::parameters_of( member ) ) };
			constexpr auto return_type { std::meta::return_type_of( member ) };

			JSReturn function {
				ctx->createFunction( method.m_name, invokeMethod< T, member >, static_cast< int >( parameters.size() ) )
			};

			if ( proto.setProperty( method.m_name, function ) < 0 )
				throw std::runtime_error( "Failed to install method binding" );
		}
	}

	JSReturn constructor { ctx->createConstructor( class_name, allocJSObject< T >, 0 ) };

	if ( ctx->setConstructor( constructor, proto ) < 0 )
	{
		throw std::runtime_error( "Failed to set constructor in prototype" );
	}

	ctx->setClassProto( class_id, proto );

	auto global { ctx->getGlobalObject() };

	if ( global.setProperty( class_name, constructor ) < 0 )
	{
		throw std::runtime_error( "Failed to set constructor" );
	}
}

std::shared_ptr< IDHANJSRuntime > installBindings( std::shared_ptr< IDHANJSRuntime > ctx )
{
	installBindingsFor< Headers >( ctx );

	return ctx;
}

} // namespace idhan::js