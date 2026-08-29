#include "js/bindings/IdhanBindings.hpp"

#include <json/json.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <expected>
#include <sstream>

#include "js/QuickJSBinding.hpp"
#include "js/ScriptExecution.hpp"
#include "js/bindings/FetchBindings.hpp"
#include "js/bindings/HtmlBindings.hpp"
#include "js/bindings/UrlBindings.hpp"
#include "logging/format_ns.hpp"

namespace idhan::downloader
{

void PendingPromise::settle( const bool ok, JSValue value )
{
	if ( context == nullptr )
	{
		return;
	}

	JSValue handler { ok ? resolve : reject };
	JSValue called { JS_Call( context, handler, JS_UNDEFINED, 1, &value ) };
	JS_FreeValue( context, called );
	JS_FreeValue( context, value );
	discard();
}

void PendingPromise::discard()
{
	if ( context == nullptr ) return;

	JS_FreeValue( context, resolve );
	JS_FreeValue( context, reject );
	resolve = JS_UNDEFINED;
	reject = JS_UNDEFINED;
	context = nullptr;
}

std::string scriptValueString( JSContext* context, const JSValueConst value )
{
	const char* text { JS_ToCString( context, value ) };

	if ( text == nullptr ) return "Unable to format JavaScript value";

	std::string output { text };
	JS_FreeCString( context, text );
	return std::move( output );
}

std::string scriptErrorString( JSContext* context, const JSValueConst value )
{
	std::string output { scriptValueString( context, value ) };
	JSValue stack { JS_GetPropertyStr( context, value, "stack" ) };

	if ( JS_IsException( stack ) )
	{
		JS_FreeValue( context, JS_GetException( context ) );
	}
	else if ( !JS_IsUndefined( stack ) && !JS_IsNull( stack ) )
	{
		if ( const std::string trace { scriptValueString( context, stack ) }; !trace.empty() )
			output += format_ns::format( "\n{}", trace );
	}

	JS_FreeValue( context, stack );
	return output;
}

std::string scriptExceptionString( JSContext* context )
{
	JSValue exception { JS_GetException( context ) };
	std::string output { scriptErrorString( context, exception ) };
	JS_FreeValue( context, exception );
	return output;
}

std::expected< Json::Value, std::string > scriptValueToJson( JSContext* context, const JSValueConst value )
{
	JSValue encoded { JS_JSONStringify( context, value, JS_UNDEFINED, JS_UNDEFINED ) };

	if ( JS_IsException( encoded ) ) return std::unexpected( scriptExceptionString( context ) );

	const std::string text { scriptValueString( context, encoded ) };
	JS_FreeValue( context, encoded );

	Json::Value output {};
	Json::CharReaderBuilder builder {};
	std::string errors {};
	std::istringstream stream { text };

	if ( !Json::parseFromStream( builder, stream, &output, &errors ) ) return std::unexpected( errors );

	return output;
}

static std::expected< std::optional< std::string >, std::string > requestBody(
	JSContext* context,
	const JSValueConst options )
{
	JSValue body { JS_GetPropertyStr( context, options, "body" ) };

	if ( JS_IsException( body ) ) return std::unexpected( scriptExceptionString( context ) );

	if ( JS_IsUndefined( body ) || JS_IsNull( body ) )
	{
		JS_FreeValue( context, body );
		return std::nullopt;
	}

	if ( JS_IsString( body ) )
	{
		std::size_t size {};
		const char* text { JS_ToCStringLen( context, &size, body ) };
		JS_FreeValue( context, body );

		if ( text == nullptr ) return std::unexpected( scriptExceptionString( context ) );

		std::string output { text, size };
		JS_FreeCString( context, text );
		return output;
	}

	std::size_t size {};
	std::size_t offset {};
	std::size_t length {};
	std::size_t bytes_per_element {};
	JSValue buffer { JS_GetTypedArrayBuffer( context, body, &offset, &length, &bytes_per_element ) };
	std::uint8_t* bytes {};

	if ( !JS_IsException( buffer ) )
	{
		bytes = JS_GetArrayBuffer( context, &size, buffer );

		if ( bytes != nullptr )
		{
			bytes += offset;
			size = length;
		}
	}
	else
	{
		JS_FreeValue( context, JS_GetException( context ) );
		buffer = JS_UNDEFINED;
		bytes = JS_GetArrayBuffer( context, &size, body );
	}

	if ( bytes == nullptr )
	{
		if ( JS_HasException( context ) ) JS_FreeValue( context, JS_GetException( context ) );

		JS_FreeValue( context, buffer );
		JS_FreeValue( context, body );
		return std::unexpected( "Request body must be a string, ArrayBuffer, or typed array" );
	}

	std::string output { reinterpret_cast< const char* >( bytes ), size };
	JS_FreeValue( context, buffer );
	JS_FreeValue( context, body );
	return output;
}

static bool hasRefererHeader( const Json::Value& options )
{
	const auto& headers { options[ "headers" ] };

	if ( !headers.isObject() ) return false;

	for ( const auto& name : headers.getMemberNames() )
	{
		const bool matches { std::ranges::equal(
			name,
			std::string_view { "referer" },
			[]( const char left, const char right )
			{ return std::tolower( static_cast< unsigned char >( left ) ) == right; } ) };

		if ( matches ) return true;
	}

	return false;
}

static void applyDefaultReferer( Json::Value& options, const std::string& parser_url )
{
	if ( parser_url.empty() || options.isMember( "referer" ) || hasRefererHeader( options ) ) return;

	options[ "referer" ] = parser_url;
}

static JSValue makePromise( JSContext* context, PendingPromise& promise )
{
	JSValue resolving[ 2 ] {};
	JSValue value { JS_NewPromiseCapability( context, resolving ) };

	if ( JS_IsException( value ) ) return value;

	promise.context = context;
	promise.resolve = resolving[ 0 ];
	promise.reject = resolving[ 1 ];
	return value;
}

static JSValue requestBinding( JSContext* context, JSValueConst, const int count, JSValueConst* arguments )
{
	try
	{
		if ( count == 0 || !JS_IsObject( arguments[ 0 ] ) )
			return JS_ThrowTypeError( context, "idhan.request expects an options object" );

		auto body { requestBody( context, arguments[ 0 ] ) };

		if ( !body ) return JS_ThrowTypeError( context, "%s", body.error().c_str() );

		auto options { scriptValueToJson( context, arguments[ 0 ] ) };

		if ( !options ) return JS_ThrowTypeError( context, "%s", options.error().c_str() );

		if ( body->has_value() ) ( *options )[ "body" ] = **body;

		if ( !( *options )[ "url" ].isString() || ( *options )[ "url" ].asString().empty() )
			return JS_ThrowTypeError( context, "idhan.request requires a non-empty URL" );

		auto* execution { executionFor( context ) };

		if ( execution == nullptr || execution->session == nullptr )
			return JS_ThrowInternalError( context, "idhan.request has no download session" );

		applyDefaultReferer( *options, execution->parser_url );

		PendingPromise promise {};
		JSValue value { makePromise( context, promise ) };

		if ( JS_IsException( value ) ) return value;

		execution->session->startRequest( *execution, std::move( *options ), promise );
		return value;
	}
	IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )
}

static JSValue importBinding( JSContext* context, JSValueConst, const int count, JSValueConst* arguments )
{
	try
	{
		if ( count == 0 || !JS_IsObject( arguments[ 0 ] ) )
			return JS_ThrowTypeError( context, "idhan.import expects an options object" );

		auto options { scriptValueToJson( context, arguments[ 0 ] ) };

		if ( !options ) return JS_ThrowTypeError( context, "%s", options.error().c_str() );

		const auto& request { ( *options )[ "request" ] };

		if ( !request.isObject() || !request[ "url" ].isString() || request[ "url" ].asString().empty() )
			return JS_ThrowTypeError( context, "idhan.import requires request.url" );

		auto* execution { executionFor( context ) };

		if ( execution == nullptr || execution->session == nullptr )
			return JS_ThrowInternalError( context, "idhan.import has no download session" );

		applyDefaultReferer( ( *options )[ "request" ], execution->parser_url );

		execution->session->startImport( *execution, std::move( *options ) );
		return JS_UNDEFINED;
	}
	IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )
}

static std::optional< std::string > requiredStringProperty(
	JSContext* context,
	const JSValueConst object,
	const char* property,
	const char* operation )
{
	JSValue value { JS_GetPropertyStr( context, object, property ) };

	if ( JS_IsException( value ) ) return std::nullopt;

	if ( !JS_IsString( value ) )
	{
		JS_FreeValue( context, value );
		JS_ThrowTypeError( context, "%s requires a string '%s' property", operation, property );
		return std::nullopt;
	}

	std::size_t size {};
	const char* text { JS_ToCStringLen( context, &size, value ) };
	JS_FreeValue( context, value );

	if ( text == nullptr ) return std::nullopt;

	std::string result { text, size };
	JS_FreeCString( context, text );
	return result;
}

static JSValue followBinding( JSContext* context, JSValueConst, const int count, JSValueConst* arguments )
{
	try
	{
		if ( count == 0 || !JS_IsObject( arguments[ 0 ] ) )
			return JS_ThrowTypeError( context, "idhan.follow expects an options object" );

		const auto url { requiredStringProperty( context, arguments[ 0 ], "url", "idhan.follow" ) };

		if ( !url.has_value() ) return JS_EXCEPTION;
		if ( url->empty() ) return JS_ThrowTypeError( context, "idhan.follow requires a non-empty URL" );

		auto* execution { executionFor( context ) };

		if ( execution == nullptr || execution->session == nullptr )
			return JS_ThrowInternalError( context, "idhan.follow has no download session" );

		const auto result { execution->session->follow( *execution, *url ) };

		switch ( result.status )
		{
			case FollowStatus::QUEUED:
				spdlog::debug( "downloader follow: queued {} as work {}", *url, result.work_id );
				break;
			case FollowStatus::FILTERED:
				spdlog::debug( "downloader follow: filtered {}", *url );
				break;
			case FollowStatus::ALREADY_QUEUED:
				spdlog::debug( "downloader follow: {} is already queued as work {}", *url, result.work_id );
				break;
			case FollowStatus::ALREADY_EXPLORED:
				spdlog::debug( "downloader follow: {} was already explored", *url );
				break;
			case FollowStatus::ALREADY_IMPORTED:
				spdlog::debug( "downloader follow: {} is already record {}", *url, result.record_id );
				break;
		}

		return JS_UNDEFINED;
	}
	IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )
}

static JSValue secretBinding( JSContext* context, JSValueConst, const int count, JSValueConst* arguments )
{
	try
	{
		if ( count == 0 || !JS_IsString( arguments[ 0 ] ) )
			return JS_ThrowTypeError( context, "idhan.secret expects a secret name" );

		std::size_t size {};
		const char* text { JS_ToCStringLen( context, &size, arguments[ 0 ] ) };

		if ( text == nullptr ) return JS_EXCEPTION;

		const std::string name { text, size };
		JS_FreeCString( context, text );

		if ( name.empty() ) return JS_ThrowTypeError( context, "idhan.secret requires a non-empty secret name" );

		auto* execution { executionFor( context ) };

		if ( execution == nullptr || execution->session == nullptr )
			return JS_ThrowInternalError( context, "idhan.secret has no download session" );

		const auto value { execution->session->secret( name ) };

		if ( !value.has_value() ) return JS_NULL;

		return JS_NewStringLen( context, value->data(), value->size() );
	}
	IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )
}

static JSValue consoleLog( JSContext* context, JSValueConst, const int count, JSValueConst* arguments )
{
	std::string message {};

	for ( int index {}; index < count; ++index )
	{
		if ( index != 0 ) message += ' ';

		if ( const char* value { JS_ToCString( context, arguments[ index ] ) }; value != nullptr )
		{
			message += value;
			JS_FreeCString( context, value );
		}
	}

	spdlog::info( "Script: {}", message );
	return JS_UNDEFINED;
}

static bool setFunction(
	JSContext* context,
	const JSValueConst object,
	const char* name,
	JSCFunction* function,
	const int arguments )
{
	return JS_SetPropertyStr( context, object, name, JS_NewCFunction( context, function, name, arguments ) ) >= 0;
}

std::expected< void, std::string > installIdhanBindings( JSContext* context )
{
	JSValue global { JS_GetGlobalObject( context ) };
	JSValue idhan { JS_NewObject( context ) };

	bool success { setFunction( context, idhan, "request", requestBinding, 1 ) };
	success = setFunction( context, idhan, "import", importBinding, 1 ) && success;
	success = setFunction( context, idhan, "follow", followBinding, 1 ) && success;
	success = setFunction( context, idhan, "secret", secretBinding, 1 ) && success;
	success = setFunction( context, idhan, "parseHtml", quickjs::parseHTML, 1 ) && success;
	success = JS_SetPropertyStr( context, global, "idhan", idhan ) >= 0 && success;

	JSValue console { JS_NewObject( context ) };
	success = setFunction( context, console, "log", consoleLog, 1 ) && success;
	success = setFunction( context, console, "error", consoleLog, 1 ) && success;
	success = JS_SetPropertyStr( context, global, "console", console ) >= 0 && success;

	const bool urls { quickjs::installURLBindings( context, global ) };
	JS_FreeValue( context, global );

	if ( !success ) return std::unexpected( "Unable to install the idhan bindings" );
	if ( !urls ) return std::unexpected( "Unable to install the URL bindings" );
	if ( !quickjs::installHTMLBindings( context ) ) return std::unexpected( "Unable to install the HTML bindings" );
	if ( const auto fetch { installFetchBindings( context ) }; !fetch )
		return std::unexpected( format_ns::format( "Unable to install the fetch bindings: {}", fetch.error() ) );

	return {};
}

} // namespace idhan::downloader
