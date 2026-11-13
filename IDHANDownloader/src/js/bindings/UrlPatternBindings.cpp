#include "js/bindings/UrlPatternBindings.hpp"

#include <ada.h>
#include <array>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>

#include "js/CommonBindings.hpp"
#include "js/JSValueHandle.hpp"
#include "js/QuickJSBinding.hpp"
#include "js/QuickJSRegexProvider.hpp"
#include "js/bindings/IdhanBindings.hpp"

namespace idhan::downloader::quickjs::url_pattern
{
using Pattern = ada::url_pattern< QuickJSRegexProvider >;
using Input = std::variant< std::string, ada::url_pattern_init >;

JSClassID allocateClassID()
{
	JSClassID value {};
	return JS_NewClassID( &value );
}

//! Allocate before worker threads create realms; installClass only reads this ID.
JSClassID class_id { allocateClassID() };

struct Field
{
	const char* name;
	std::optional< std::string > ada::url_pattern_init::* member;
};

//! Web IDL reads dictionary members in lexicographic order, including inherited
//! properties. Convert each member once, preserving exceptions from getters.
constexpr std::array fields {
	Field { "baseURL", &ada::url_pattern_init::base_url },  Field { "hash", &ada::url_pattern_init::hash },
	Field { "hostname", &ada::url_pattern_init::hostname }, Field { "password", &ada::url_pattern_init::password },
	Field { "pathname", &ada::url_pattern_init::pathname }, Field { "port", &ada::url_pattern_init::port },
	Field { "protocol", &ada::url_pattern_init::protocol }, Field { "search", &ada::url_pattern_init::search },
	Field { "username", &ada::url_pattern_init::username }
};

struct Arguments
{
	Input input;
	std::optional< std::string > base;
	ada::url_pattern_options options;

	ada::url_pattern_input view() const
	{
		if ( std::holds_alternative< std::string >( input ) )
		{
			return std::string_view { std::get< std::string >( input ) };
		}

		return std::get< ada::url_pattern_init >( input );
	}
};

//! Conversion failures leave the original JavaScript exception pending.
std::optional< Input > readInput( JSContext* context, const JSValueConst value )
{
	if ( JS_IsUndefined( value ) || JS_IsNull( value ) )
	{
		return ada::url_pattern_init {};
	}

	if ( !JS_IsObject( value ) )
	{
		auto text { toString( context, value ) };

		if ( !text.has_value() )
		{
			return std::nullopt;
		}

		return std::move( text.value() );
	}

	ada::url_pattern_init init;

	for ( const auto& field : fields )
	{
		JSValueHandle property { context, JS_GetPropertyStr( context, value, field.name ) };

		if ( JS_IsException( property.get() ) )
		{
			return std::nullopt;
		}

		if ( JS_IsUndefined( property.get() ) )
		{
			continue;
		}

		auto text { toString( context, property.get() ) };

		if ( !text.has_value() )
		{
			return std::nullopt;
		}

		init.*( field.member ) = std::move( text.value() );
	}

	return init;
}

std::optional< ada::url_pattern_options > readOptions( JSContext* context, const JSValueConst value )
{
	if ( JS_IsUndefined( value ) || JS_IsNull( value ) )
	{
		return ada::url_pattern_options {};
	}

	if ( !JS_IsObject( value ) )
	{
		JS_ThrowTypeError( context, "URLPattern options must be an object" );
		return std::nullopt;
	}

	JSValueHandle ignore_case { context, JS_GetPropertyStr( context, value, "ignoreCase" ) };

	if ( JS_IsException( ignore_case.get() ) )
	{
		return std::nullopt;
	}

	return ada::url_pattern_options { .ignore_case = JS_ToBool( context, ignore_case.get() ) != 0 };
}

std::optional< Arguments > readArguments(
	JSContext* context,
	const int count,
	JSValueConst* values,
	const bool constructing )
{
	auto input { readInput( context, count > 0 ? values[ 0 ] : JS_UNDEFINED ) };

	if ( !input.has_value() )
	{
		return std::nullopt;
	}

	Arguments result { .input = std::move( input.value() ), .base = {}, .options = {} };
	const JSValueConst second { count > 1 ? values[ 1 ] : JS_UNDEFINED };
	const bool options_overload {
		constructing && count < 3 && ( JS_IsObject( second ) || JS_IsNull( second ) || JS_IsUndefined( second ) )
	};

	const bool base_required { constructing && count > 2 };

	if ( !options_overload && ( base_required || !JS_IsUndefined( second ) ) )
	{
		auto base { toString( context, second ) };

		if ( !base.has_value() )
		{
			return std::nullopt;
		}

		result.base = std::move( base.value() );
	}

	if ( constructing )
	{
		const JSValueConst options_value { options_overload ? second : ( count > 2 ? values[ 2 ] : JS_UNDEFINED ) };
		const auto options { readOptions( context, options_value ) };

		if ( !options.has_value() )
		{
			return std::nullopt;
		}

		result.options = options.value();
	}

	return result;
}

void finalize( JSRuntime*, const JSValue value )
{
	delete static_cast< Pattern* >( JS_GetOpaque( value, class_id ) );
}

IDHAN_QJS_CONSTRUCTOR( construct )
{
	try
	{
		const auto input { readArguments( context, argument_count, arguments, true ) };

		if ( !input.has_value() )
		{
			return JS_EXCEPTION;
		}

		const auto& parsed { input.value() };
		const std::string_view base {
			parsed.base.has_value() ? std::string_view { parsed.base.value() } : std::string_view {}
		};
		QuickJSRegexProvider::Scope scope { context };
		auto pattern_e { ada::parse_url_pattern< QuickJSRegexProvider >(
			parsed.view(), parsed.base.has_value() ? &base : nullptr, &parsed.options ) };

		if ( JS_HasException( context ) )
		{
			return JS_EXCEPTION;
		}

		if ( !pattern_e.has_value() )
		{
			return JS_ThrowTypeError( context, "Invalid URLPattern" );
		}

		auto pattern { std::make_unique< Pattern >( std::move( pattern_e.value() ) ) };
		JSValueHandle prototype { context, JS_GetPropertyStr( context, new_target, "prototype" ) };

		if ( JS_IsException( prototype.get() ) )
		{
			return JS_EXCEPTION;
		}

		JSValueHandle fallback { context, JS_GetClassProto( context, class_id ) };
		const JSValueConst selected { JS_IsObject( prototype.get() ) ? prototype.get() : fallback.get() };
		JSValue value { JS_NewObjectProtoClass( context, selected, class_id ) };

		if ( JS_IsException( value ) )
		{
			return JS_EXCEPTION;
		}

		JS_SetOpaque( value, pattern.release() );
		return value;
	}
	IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )
}

bool defineProperty( JSContext* context, const JSValueConst object, const char* name, JSValue value )
{
	if ( JS_IsException( value ) )
	{
		return false;
	}

	return JS_DefinePropertyValueStr( context, object, name, value, JS_PROP_C_W_E ) >= 0;
}

JSValue serializeInput( JSContext* context, const ada::url_pattern_input& input )
{
	if ( std::holds_alternative< std::string_view >( input ) )
	{
		const auto text { std::get< std::string_view >( input ) };
		return JS_NewStringLen( context, text.data(), text.size() );
	}

	const auto& init { std::get< ada::url_pattern_init >( input ) };
	JSValueHandle object { context, JS_NewObject( context ) };

	if ( JS_IsException( object.get() ) )
	{
		return JS_EXCEPTION;
	}

	for ( const auto& field : fields )
	{
		const auto& text { init.*( field.member ) };

		if ( !text.has_value() )
		{
			continue;
		}

		if ( !defineProperty(
				 context, object.get(), field.name, JS_NewStringLen( context, text->data(), text->size() ) ) )
		{
			return JS_EXCEPTION;
		}
	}

	return object.release();
}

JSValue serializeComponent( JSContext* context, const ada::url_pattern_component_result& component )
{
	JSValueHandle object { context, JS_NewObject( context ) };
	JSValueHandle groups { context, JS_NewObjectProto( context, JS_NULL ) };

	if ( JS_IsException( object.get() ) || JS_IsException( groups.get() ) )
	{
		return JS_EXCEPTION;
	}

	if ( !defineProperty(
			 context,
			 object.get(),
			 "input",
			 JS_NewStringLen( context, component.input.data(), component.input.size() ) ) )
	{
		return JS_EXCEPTION;
	}

	for ( const auto& [ name, capture ] : component.groups )
	{
		JSValue value {
			capture.has_value() ? JS_NewStringLen( context, capture->data(), capture->size() ) : JS_UNDEFINED
		};

		if ( !defineProperty( context, groups.get(), name.c_str(), value ) )
		{
			return JS_EXCEPTION;
		}
	}

	if ( !defineProperty( context, object.get(), "groups", groups.release() ) )
	{
		return JS_EXCEPTION;
	}

	return object.release();
}

JSValue serializeResult( JSContext* context, const ada::url_pattern_result& result )
{
	JSValueHandle object { context, JS_NewObject( context ) };
	JSValueHandle inputs { context, JS_NewArray( context ) };

	if ( JS_IsException( object.get() ) || JS_IsException( inputs.get() ) )
	{
		return JS_EXCEPTION;
	}

	for ( std::uint32_t index {}; index < result.inputs.size(); ++index )
	{
		JSValue value { serializeInput( context, result.inputs[ index ] ) };

		if ( JS_IsException( value ) )
		{
			return JS_EXCEPTION;
		}

		if ( JS_DefinePropertyValueUint32( context, inputs.get(), index, value, JS_PROP_C_W_E ) < 0 )
		{
			return JS_EXCEPTION;
		}
	}

	if ( !defineProperty( context, object.get(), "inputs", inputs.release() ) )
	{
		return JS_EXCEPTION;
	}

	constexpr std::array components {
		std::pair { "protocol", &ada::url_pattern_result::protocol },
		std::pair { "username", &ada::url_pattern_result::username },
		std::pair { "password", &ada::url_pattern_result::password },
		std::pair { "hostname", &ada::url_pattern_result::hostname },
		std::pair { "port", &ada::url_pattern_result::port },
		std::pair { "pathname", &ada::url_pattern_result::pathname },
		std::pair { "search", &ada::url_pattern_result::search },
		std::pair { "hash", &ada::url_pattern_result::hash }
	};

	for ( const auto& [ name, member ] : components )
	{
		if ( !defineProperty( context, object.get(), name, serializeComponent( context, result.*member ) ) )
		{
			return JS_EXCEPTION;
		}
	}

	return object.release();
}

JSValue match(
	JSContext* context,
	const JSValueConst this_value,
	const int count,
	JSValueConst* values,
	const bool test_only )
{
	try
	{
		auto* pattern { static_cast< Pattern* >( JS_GetOpaque2( context, this_value, class_id ) ) };

		if ( pattern == nullptr )
		{
			return JS_EXCEPTION;
		}

		const auto input { readArguments( context, count, values, false ) };

		if ( !input.has_value() )
		{
			return JS_EXCEPTION;
		}

		const auto& parsed { input.value() };
		const std::string_view base {
			parsed.base.has_value() ? std::string_view { parsed.base.value() } : std::string_view {}
		};
		QuickJSRegexProvider::Scope scope { context };
		const auto input_view { parsed.view() };
		const auto* base_pointer { parsed.base.has_value() ? &base : nullptr };

		if ( test_only )
		{
			const auto result_e { pattern->test( input_view, base_pointer ) };

			if ( JS_HasException( context ) )
			{
				return JS_EXCEPTION;
			}

			if ( !result_e.has_value() )
			{
				return JS_ThrowTypeError( context, "A URLPatternInit input cannot have a separate base URL" );
			}

			return JS_NewBool( context, result_e.value() );
		}

		const auto result_e { pattern->exec( input_view, base_pointer ) };

		if ( JS_HasException( context ) )
		{
			return JS_EXCEPTION;
		}

		if ( !result_e.has_value() )
		{
			return JS_ThrowTypeError( context, "A URLPatternInit input cannot have a separate base URL" );
		}

		const auto& result { result_e.value() };

		if ( !result.has_value() )
		{
			return JS_NULL;
		}

		return serializeResult( context, result.value() );
	}
	IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )
}

IDHAN_QJS_FUNCTION( test )
{
	return match( context, this_value, argument_count, arguments, true );
}

IDHAN_QJS_FUNCTION( exec )
{
	return match( context, this_value, argument_count, arguments, false );
}

#define IDHAN_URL_PATTERN_GETTER( name )                                                                               \
	IDHAN_QJS_GETTER( name )                                                                                           \
	{                                                                                                                  \
		const auto* pattern { static_cast< Pattern* >( JS_GetOpaque2( context, this_value, class_id ) ) };             \
		if ( pattern == nullptr ) return JS_EXCEPTION;                                                                 \
                                                                                                                       \
		const auto text { pattern->get_##name() };                                                                     \
		return JS_NewStringLen( context, text.data(), text.size() );                                                   \
	}

IDHAN_URL_PATTERN_GETTER( protocol )
IDHAN_URL_PATTERN_GETTER( username )
IDHAN_URL_PATTERN_GETTER( password )
IDHAN_URL_PATTERN_GETTER( hostname )
IDHAN_URL_PATTERN_GETTER( port )
IDHAN_URL_PATTERN_GETTER( pathname )
IDHAN_URL_PATTERN_GETTER( search )
IDHAN_URL_PATTERN_GETTER( hash )

#undef IDHAN_URL_PATTERN_GETTER

IDHAN_QJS_GETTER( hasRegExpGroups )
{
	const auto* pattern { static_cast< Pattern* >( JS_GetOpaque2( context, this_value, class_id ) ) };

	if ( pattern == nullptr )
	{
		return JS_EXCEPTION;
	}

	return JS_NewBool( context, pattern->has_regexp_groups() );
}

const JSClassDef definition {
	.class_name = "URLPattern",
	.finalizer = finalize,
	.gc_mark = nullptr,
	.call = nullptr,
	.exotic = nullptr
};

JSCFunctionListEntry attribute( const char* name, Getter getter )
{
	auto entry { property( name, getter, nullptr ) };
	entry.prop_flags |= JS_PROP_ENUMERABLE;
	return entry;
}

JSCFunctionListEntry operation( const char* name, JSCFunction* function )
{
	auto entry { method( name, 0, function ) };
	entry.prop_flags |= JS_PROP_ENUMERABLE;
	return entry;
}

const JSCFunctionListEntry properties[] {
	attribute( "protocol", protocol ),
	attribute( "username", username ),
	attribute( "password", password ),
	attribute( "hostname", hostname ),
	attribute( "port", port ),
	attribute( "pathname", pathname ),
	attribute( "search", search ),
	attribute( "hash", hash ),
	attribute( "hasRegExpGroups", hasRegExpGroups ),
	operation( "test", test ),
	operation( "exec", exec )
};

bool installTag( JSContext* context, const JSValueConst global )
{
	JSValueHandle symbol { context, JS_GetPropertyStr( context, global, "Symbol" ) };

	if ( JS_IsException( symbol.get() ) )
	{
		return false;
	}

	JSValueHandle tag { context, JS_GetPropertyStr( context, symbol.get(), "toStringTag" ) };

	if ( JS_IsException( tag.get() ) )
	{
		return false;
	}

	const JSAtom atom { JS_ValueToAtom( context, tag.get() ) };

	if ( atom == JS_ATOM_NULL )
	{
		return false;
	}

	JSValueHandle prototype { context, JS_GetClassProto( context, class_id ) };
	const int result { JS_DefinePropertyValue(
		context, prototype.get(), atom, JS_NewString( context, "URLPattern" ), JS_PROP_CONFIGURABLE ) };
	JS_FreeAtom( context, atom );
	return result >= 0;
}
} // namespace idhan::downloader::quickjs::url_pattern

namespace idhan::downloader
{
std::expected< void, std::string > installURLPatternBindings( JSContext* context )
{
	JSValueHandle global { context, JS_GetGlobalObject( context ) };
	const bool installed { quickjs::installClass(
		context,
		global.get(),
		quickjs::url_pattern::class_id,
		quickjs::url_pattern::definition,
		quickjs::url_pattern::construct,
		0,
		quickjs::url_pattern::properties ) };

	if ( !installed )
	{
		return std::unexpected( scriptExceptionString( context ) );
	}

	if ( !quickjs::url_pattern::installTag( context, global.get() ) )
	{
		return std::unexpected( scriptExceptionString( context ) );
	}

	return {};
}
} // namespace idhan::downloader
