#include "js/bindings/UrlBindings.hpp"

#include <ada.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "js/CommonBindings.hpp"
#include "js/QuickJSBinding.hpp"

namespace idhan::downloader::quickjs
{

struct URLState
{
	explicit URLState( ada::url_aggregator&& input ) noexcept : value { std::move( input ) } {}

	ada::url_aggregator value;
};

struct URLObject
{
	std::shared_ptr< URLState > state;
};

struct URLSearchParamsObject
{
	std::shared_ptr< URLState > url {};
	ada::url_search_params parameters {};
};

JSClassID url_class_id {};
JSClassID url_search_params_class_id {};

std::optional< std::string > argumentString(
	JSContext* context,
	const int argument_count,
	JSValueConst* arguments,
	const int index )
{
	return toString( context, index < argument_count ? arguments[ index ] : JS_UNDEFINED );
}

JSValue newString( JSContext* context, const std::string_view value )
{
	return JS_NewStringLen( context, value.data(), value.size() );
}

URLObject* getURL( JSContext* context, const JSValueConst value )
{
	return static_cast< URLObject* >( JS_GetOpaque2( context, value, url_class_id ) );
}

URLSearchParamsObject* getURLSearchParams( JSContext* context, const JSValueConst value )
{
	return static_cast< URLSearchParamsObject* >( JS_GetOpaque2( context, value, url_search_params_class_id ) );
}

template < typename Object >
JSValue createObject(
	JSContext* context,
	const JSValueConst new_target,
	const JSClassID class_id,
	std::unique_ptr< Object > object )
{
	JSValue prototype { JS_GetPropertyStr( context, new_target, "prototype" ) };

	if ( JS_IsException( prototype ) ) return JS_EXCEPTION;

	JSValue value { JS_NewObjectProtoClass( context, prototype, class_id ) };
	JS_FreeValue( context, prototype );

	if ( JS_IsException( value ) ) return JS_EXCEPTION;

	JS_SetOpaque( value, object.release() );
	return value;
}

JSValue createSearchParams( JSContext* context, std::unique_ptr< URLSearchParamsObject > object )
{
	JSValue value { JS_NewObjectClass( context, static_cast< int >( url_search_params_class_id ) ) };

	if ( JS_IsException( value ) ) return JS_EXCEPTION;

	JS_SetOpaque( value, object.release() );
	return value;
}

void refresh( URLSearchParamsObject& object )
{
	if ( object.url ) object.parameters.reset( object.url->value.get_search() );
}

void commit( URLSearchParamsObject& object )
{
	if ( object.url ) object.url->value.set_search( object.parameters.to_string() );
}

void urlFinalizer( JSRuntime*, const JSValue value )
{
	delete static_cast< URLObject* >( JS_GetOpaque( value, url_class_id ) );
}

void urlSearchParamsFinalizer( JSRuntime*, const JSValue value )
{
	delete static_cast< URLSearchParamsObject* >( JS_GetOpaque( value, url_search_params_class_id ) );
}

IDHAN_QJS_CONSTRUCTOR( urlConstructor )
{
	try
	{
		if ( argument_count == 0 ) return JS_ThrowTypeError( context, "URL requires an input" );

		const auto input { argumentString( context, argument_count, arguments, 0 ) };

		if ( !input.has_value() ) return JS_EXCEPTION;

		ada::result< ada::url_aggregator > parsed {};

		if ( argument_count > 1 && !JS_IsUndefined( arguments[ 1 ] ) )
		{
			const auto base_input { argumentString( context, argument_count, arguments, 1 ) };

			if ( !base_input.has_value() ) return JS_EXCEPTION;

			auto base { ada::parse< ada::url_aggregator >( *base_input ) };

			if ( !base ) return JS_ThrowTypeError( context, "Invalid base URL: %s", base_input->c_str() );

			parsed = ada::parse< ada::url_aggregator >( *input, &( *base ) );
		}
		else
		{
			parsed = ada::parse< ada::url_aggregator >( *input );
		}

		if ( !parsed ) return JS_ThrowTypeError( context, "Invalid URL: %s", input->c_str() );

		auto state { std::make_shared< URLState >( std::move( *parsed ) ) };
		return createObject( context, new_target, url_class_id, std::make_unique< URLObject >( std::move( state ) ) );
	}
	IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )
}

IDHAN_QJS_GETTER( urlHref )
{
	const auto* object { getURL( context, this_value ) };
	return object == nullptr ? JS_EXCEPTION : newString( context, object->state->value.get_href() );
}

IDHAN_QJS_SETTER( urlHrefSet )
{
	try
	{
		auto* object { getURL( context, this_value ) };

		if ( object == nullptr ) return JS_EXCEPTION;

		const auto input { toString( context, value ) };

		if ( !input.has_value() ) return JS_EXCEPTION;

		if ( !object->state->value.set_href( *input ) )
			return JS_ThrowTypeError( context, "Invalid URL: %s", input->c_str() );

		return JS_UNDEFINED;
	}
	IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )
}

IDHAN_QJS_GETTER( urlOrigin )
{
	try
	{
		const auto* object { getURL( context, this_value ) };
		return object == nullptr ? JS_EXCEPTION : newString( context, object->state->value.get_origin() );
	}
	IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )
}

#define IDHAN_URL_COMPONENT( name, getter, setter )                                                                    \
	IDHAN_QJS_GETTER( name )                                                                                           \
	{                                                                                                                  \
		const auto* object { getURL( context, this_value ) };                                                          \
		return object == nullptr ? JS_EXCEPTION : newString( context, object->state->value.getter() );                 \
	}                                                                                                                  \
	IDHAN_QJS_SETTER( name##Set )                                                                                      \
	{                                                                                                                  \
		try                                                                                                            \
		{                                                                                                              \
			auto* object { getURL( context, this_value ) };                                                            \
			if ( object == nullptr ) return JS_EXCEPTION;                                                              \
			const auto input { toString( context, value ) };                                                           \
			if ( !input.has_value() ) return JS_EXCEPTION;                                                             \
			if ( !object->state->value.setter( *input ) )                                                              \
				return JS_ThrowTypeError( context, "Invalid URL component: %s", input->c_str() );                      \
			return JS_UNDEFINED;                                                                                       \
		}                                                                                                              \
		IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )                                                                      \
	}

IDHAN_URL_COMPONENT( urlProtocol, get_protocol, set_protocol )
IDHAN_URL_COMPONENT( urlHost, get_host, set_host )
IDHAN_URL_COMPONENT( urlHostname, get_hostname, set_hostname )
IDHAN_URL_COMPONENT( urlPort, get_port, set_port )
IDHAN_URL_COMPONENT( urlPathname, get_pathname, set_pathname )

#undef IDHAN_URL_COMPONENT

#define IDHAN_URL_VOID_COMPONENT( name, getter, setter )                                                               \
	IDHAN_QJS_GETTER( name )                                                                                           \
	{                                                                                                                  \
		const auto* object { getURL( context, this_value ) };                                                          \
		return object == nullptr ? JS_EXCEPTION : newString( context, object->state->value.getter() );                 \
	}                                                                                                                  \
	IDHAN_QJS_SETTER( name##Set )                                                                                      \
	{                                                                                                                  \
		try                                                                                                            \
		{                                                                                                              \
			auto* object { getURL( context, this_value ) };                                                            \
			if ( object == nullptr ) return JS_EXCEPTION;                                                              \
			const auto input { toString( context, value ) };                                                           \
			if ( !input.has_value() ) return JS_EXCEPTION;                                                             \
			object->state->value.setter( *input );                                                                     \
			return JS_UNDEFINED;                                                                                       \
		}                                                                                                              \
		IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )                                                                      \
	}

IDHAN_URL_VOID_COMPONENT( urlSearch, get_search, set_search )
IDHAN_URL_VOID_COMPONENT( urlHash, get_hash, set_hash )

#undef IDHAN_URL_VOID_COMPONENT

IDHAN_QJS_GETTER( urlSearchParams )
{
	try
	{
		const auto* object { getURL( context, this_value ) };

		if ( object == nullptr ) return JS_EXCEPTION;

		auto parameters { std::make_unique< URLSearchParamsObject >() };
		parameters->url = object->state;
		refresh( *parameters );
		return createSearchParams( context, std::move( parameters ) );
	}
	IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )
}

IDHAN_QJS_FUNCTION( urlToString )
{
	(void)argument_count;
	(void)arguments;
	return urlHref( context, this_value );
}

static bool appendPairs( JSContext* context, const JSValueConst input, ada::url_search_params& parameters )
{
	JSValue length { JS_GetPropertyStr( context, input, "length" ) };
	std::uint32_t count {};
	const int converted { JS_ToUint32( context, &count, length ) };
	JS_FreeValue( context, length );

	if ( converted < 0 ) return false;

	for ( std::uint32_t index {}; index < count; ++index )
	{
		JSValue pair { JS_GetPropertyUint32( context, input, index ) };

		if ( JS_IsException( pair ) ) return false;

		JSValue name { JS_GetPropertyUint32( context, pair, 0 ) };
		JSValue value { JS_GetPropertyUint32( context, pair, 1 ) };
		const auto name_text { toString( context, name ) };
		const auto value_text { toString( context, value ) };
		JS_FreeValue( context, name );
		JS_FreeValue( context, value );
		JS_FreeValue( context, pair );

		if ( !name_text.has_value() || !value_text.has_value() ) return false;

		parameters.append( *name_text, *value_text );
	}

	return true;
}

static bool appendRecord( JSContext* context, const JSValueConst input, ada::url_search_params& parameters )
{
	JSPropertyEnum* properties {};
	std::uint32_t count {};

	if ( JS_GetOwnPropertyNames( context, &properties, &count, input, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY ) < 0 )
		return false;

	bool success { true };

	for ( std::uint32_t index {}; index < count && success; ++index )
	{
		JSValue name { JS_AtomToString( context, properties[ index ].atom ) };
		JSValue value { JS_GetProperty( context, input, properties[ index ].atom ) };
		const auto name_text { toString( context, name ) };
		const auto value_text { toString( context, value ) };
		JS_FreeValue( context, name );
		JS_FreeValue( context, value );

		if ( name_text.has_value() && value_text.has_value() )
			parameters.append( *name_text, *value_text );
		else
			success = false;
	}

	JS_FreePropertyEnum( context, properties, count );
	return success;
}

IDHAN_QJS_CONSTRUCTOR( urlSearchParamsConstructor )
{
	try
	{
		auto object { std::make_unique< URLSearchParamsObject >() };

		if ( argument_count > 0 && !JS_IsUndefined( arguments[ 0 ] ) && !JS_IsNull( arguments[ 0 ] ) )
		{
			const JSValueConst input { arguments[ 0 ] };

			if ( JS_IsObject( input ) )
			{
				const int array { JS_IsArray( context, input ) };

				if ( array < 0 ) return JS_EXCEPTION;

				const bool filled { array > 0 ? appendPairs( context, input, object->parameters ) :
					                            appendRecord( context, input, object->parameters ) };

				if ( !filled ) return JS_EXCEPTION;
			}
			else
			{
				const auto text { argumentString( context, argument_count, arguments, 0 ) };

				if ( !text.has_value() ) return JS_EXCEPTION;

				object->parameters.reset( *text );
			}
		}

		return createObject( context, new_target, url_search_params_class_id, std::move( object ) );
	}
	IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )
}

bool requireArguments( JSContext* context, const int actual, const int required, const char* function )
{
	if ( actual >= required ) return true;

	JS_ThrowTypeError( context, "%s requires %d argument%s", function, required, required == 1 ? "" : "s" );
	return false;
}

IDHAN_QJS_FUNCTION( searchParamsGet )
{
	try
	{
		if ( !requireArguments( context, argument_count, 1, "URLSearchParams.get" ) ) return JS_EXCEPTION;

		auto* object { getURLSearchParams( context, this_value ) };
		const auto key { argumentString( context, argument_count, arguments, 0 ) };

		if ( object == nullptr || !key.has_value() ) return JS_EXCEPTION;

		refresh( *object );
		const auto value { object->parameters.get( *key ) };
		return value.has_value() ? newString( context, *value ) : JS_NULL;
	}
	IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )
}

IDHAN_QJS_FUNCTION( searchParamsGetAll )
{
	try
	{
		if ( !requireArguments( context, argument_count, 1, "URLSearchParams.getAll" ) ) return JS_EXCEPTION;

		auto* object { getURLSearchParams( context, this_value ) };
		const auto key { argumentString( context, argument_count, arguments, 0 ) };

		if ( object == nullptr || !key.has_value() ) return JS_EXCEPTION;

		refresh( *object );
		const auto values { object->parameters.get_all( *key ) };
		JSValue result { JS_NewArray( context ) };

		if ( JS_IsException( result ) ) return result;

		for ( std::size_t index {}; index < values.size(); ++index )
		{
			if ( JS_SetPropertyUint32(
					 context, result, static_cast< std::uint32_t >( index ), newString( context, values[ index ] ) )
			     < 0 )
			{
				JS_FreeValue( context, result );
				return JS_EXCEPTION;
			}
		}

		return result;
	}
	IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )
}

IDHAN_QJS_FUNCTION( searchParamsHas )
{
	try
	{
		if ( !requireArguments( context, argument_count, 1, "URLSearchParams.has" ) ) return JS_EXCEPTION;

		auto* object { getURLSearchParams( context, this_value ) };
		const auto key { argumentString( context, argument_count, arguments, 0 ) };

		if ( object == nullptr || !key.has_value() ) return JS_EXCEPTION;

		refresh( *object );
		return JS_NewBool( context, object->parameters.has( *key ) );
	}
	IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )
}

template < typename Operation >
JSValue mutateSearchParams(
	JSContext* context,
	const JSValueConst this_value,
	const int argument_count,
	JSValueConst* arguments,
	const int required_arguments,
	const char* function,
	Operation operation )
{
	if ( !requireArguments( context, argument_count, required_arguments, function ) ) return JS_EXCEPTION;

	auto* object { getURLSearchParams( context, this_value ) };

	if ( object == nullptr ) return JS_EXCEPTION;

	const auto key { argumentString( context, argument_count, arguments, 0 ) };

	if ( !key.has_value() ) return JS_EXCEPTION;

	std::optional< std::string > value {};

	if ( required_arguments > 1 )
	{
		value = argumentString( context, argument_count, arguments, 1 );

		if ( !value.has_value() ) return JS_EXCEPTION;
	}

	refresh( *object );
	operation( object->parameters, *key, value );
	commit( *object );
	return JS_UNDEFINED;
}

IDHAN_QJS_FUNCTION( searchParamsAppend )
{
	try
	{
		return mutateSearchParams(
			context,
			this_value,
			argument_count,
			arguments,
			2,
			"URLSearchParams.append",
			[]( auto& parameters, const auto& key, const auto& value ) { parameters.append( key, *value ); } );
	}
	IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )
}

IDHAN_QJS_FUNCTION( searchParamsSet )
{
	try
	{
		return mutateSearchParams(
			context,
			this_value,
			argument_count,
			arguments,
			2,
			"URLSearchParams.set",
			[]( auto& parameters, const auto& key, const auto& value ) { parameters.set( key, *value ); } );
	}
	IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )
}

IDHAN_QJS_FUNCTION( searchParamsDelete )
{
	try
	{
		return mutateSearchParams(
			context,
			this_value,
			argument_count,
			arguments,
			1,
			"URLSearchParams.delete",
			[]( auto& parameters, const auto& key, const auto& ) { parameters.remove( key ); } );
	}
	IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )
}

IDHAN_QJS_FUNCTION( searchParamsSort )
{
	try
	{
		(void)argument_count;
		(void)arguments;
		auto* object { getURLSearchParams( context, this_value ) };

		if ( object == nullptr ) return JS_EXCEPTION;

		refresh( *object );
		object->parameters.sort();
		commit( *object );
		return JS_UNDEFINED;
	}
	IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )
}

IDHAN_QJS_FUNCTION( searchParamsToString )
{
	try
	{
		(void)argument_count;
		(void)arguments;
		auto* object { getURLSearchParams( context, this_value ) };

		if ( object == nullptr ) return JS_EXCEPTION;

		refresh( *object );
		return newString( context, object->parameters.to_string() );
	}
	IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )
}

IDHAN_QJS_GETTER( searchParamsSize )
{
	try
	{
		auto* object { getURLSearchParams( context, this_value ) };

		if ( object == nullptr ) return JS_EXCEPTION;

		refresh( *object );
		return JS_NewInt64( context, static_cast< std::int64_t >( object->parameters.size() ) );
	}
	IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )
}

const JSClassDef url_class {
	.class_name = "URL",
	.finalizer = urlFinalizer,
	.gc_mark = nullptr,
	.call = nullptr,
	.exotic = nullptr,
};

const JSClassDef url_search_params_class {
	.class_name = "URLSearchParams",
	.finalizer = urlSearchParamsFinalizer,
	.gc_mark = nullptr,
	.call = nullptr,
	.exotic = nullptr,
};

const JSCFunctionListEntry url_properties[] {
	IDHAN_QJS_PROPERTY( "href", urlHref, urlHrefSet ),
	IDHAN_QJS_READONLY_PROPERTY( "origin", urlOrigin ),
	IDHAN_QJS_PROPERTY( "protocol", urlProtocol, urlProtocolSet ),
	IDHAN_QJS_PROPERTY( "host", urlHost, urlHostSet ),
	IDHAN_QJS_PROPERTY( "hostname", urlHostname, urlHostnameSet ),
	IDHAN_QJS_PROPERTY( "port", urlPort, urlPortSet ),
	IDHAN_QJS_PROPERTY( "pathname", urlPathname, urlPathnameSet ),
	IDHAN_QJS_PROPERTY( "search", urlSearch, urlSearchSet ),
	IDHAN_QJS_PROPERTY( "hash", urlHash, urlHashSet ),
	IDHAN_QJS_READONLY_PROPERTY( "searchParams", urlSearchParams ),
	IDHAN_QJS_METHOD( "toString", 0, urlToString ),
	IDHAN_QJS_METHOD( "toJSON", 0, urlToString ),
};

const JSCFunctionListEntry url_search_params_properties[] {
	IDHAN_QJS_READONLY_PROPERTY( "size", searchParamsSize ), IDHAN_QJS_METHOD( "append", 2, searchParamsAppend ),
	IDHAN_QJS_METHOD( "delete", 1, searchParamsDelete ),     IDHAN_QJS_METHOD( "get", 1, searchParamsGet ),
	IDHAN_QJS_METHOD( "getAll", 1, searchParamsGetAll ),     IDHAN_QJS_METHOD( "has", 1, searchParamsHas ),
	IDHAN_QJS_METHOD( "set", 2, searchParamsSet ),           IDHAN_QJS_METHOD( "sort", 0, searchParamsSort ),
	IDHAN_QJS_METHOD( "toString", 0, searchParamsToString ),
};

bool installURLBindings( JSContext* context, const JSValueConst global )
{
	ada::set_max_input_length( 1024 * 1024 );

	const bool search_params_installed { installClass(
		context,
		global,
		url_search_params_class_id,
		url_search_params_class,
		urlSearchParamsConstructor,
		1,
		url_search_params_properties ) };

	if ( !search_params_installed ) return false;

	return installClass( context, global, url_class_id, url_class, urlConstructor, 2, url_properties );
}
} // namespace idhan::downloader::quickjs
