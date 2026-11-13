#include "js/bindings/IdhanBindings.hpp"

#include <json/json.h>
#include <spdlog/spdlog.h>

#include <ada.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <expected>
#include <mutex>
#include <sstream>
#include <utility>
#include <vector>

#include "IDHANDownloader/secrets/SecretDomain.hpp"
#include "js/QuickJSBinding.hpp"
#include "js/JSValueHandle.hpp"
#include "js/ScriptContext.hpp"
#include "js/ScriptExecution.hpp"
#include "js/ScriptSession.hpp"
#include "js/bindings/FetchBindings.hpp"
#include "js/bindings/WebCryptoBindings.hpp"
#include "js/bindings/HtmlBindings.hpp"
#include "js/bindings/UrlBindings.hpp"
#include "js/bindings/UrlPatternBindings.hpp"
#include "logging/format_ns.hpp"

namespace idhan::downloader
{

void PendingPromise::resolveOrReject( const bool ok, JSValue value )
{
	const ScriptContext::ExecutionScope timing { context };
	if ( context == nullptr )
	{
		return;
	}

	JSValue handler { ok ? resolve : reject };
	JSValue called { JS_Call( context, handler, JS_UNDEFINED, 1, &value ) };
	if ( JS_IsException( called ) && ok )
	{
		// An interrupted thenable getter must reject the request rather than leave it pending.
		JSValue exception { JS_GetException( context ) };
		JSValue rejected { JS_Call( context, reject, JS_UNDEFINED, 1, &exception ) };
		JS_FreeValue( context, rejected );
		JS_FreeValue( context, exception );
	}

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
	const ScriptContext::ExecutionScope timing { context };
	const char* text { JS_ToCString( context, value ) };

	if ( text == nullptr ) return "Unable to format JavaScript value";

	std::string output { text };
	JS_FreeCString( context, text );
	return std::move( output );
}

static std::string outOfMemoryString( JSContext* context )
{
	constexpr double MiB { 1024.0 * 1024.0 };
	JSMemoryUsage usage {};
	JS_ComputeMemoryUsage( JS_GetRuntime( context ), &usage );

	return format_ns::format(
		"Parser runtime ran out of memory ({:.1f} MiB of {:.1f} MiB in use, shared by every running instance of this "
		"parser)",
		static_cast< double >( usage.malloc_size ) / MiB,
		static_cast< double >( usage.malloc_limit ) / MiB );
}

std::string scriptErrorString( JSContext* context, const JSValueConst value )
{
	const ScriptContext::ExecutionScope timing { context };

	// QuickJS throws null when it cannot allocate the Error object for a failure.
	if ( JS_IsNull( value ) ) return outOfMemoryString( context );

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
	const ScriptContext::ExecutionScope timing { context };
	JSValue exception { JS_GetException( context ) };
	std::string output { scriptErrorString( context, exception ) };
	JS_FreeValue( context, exception );
	return output;
}

std::expected< Json::Value, std::string > scriptValueToJson( JSContext* context, const JSValueConst value )
{
	const ScriptContext::ExecutionScope timing { context };
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

using UrlTypeDeclaration = std::pair< std::string, std::string >;

static std::expected< std::optional< UrlTypeDeclaration >, std::string > urlTypeDeclaration( const Json::Value& entry )
{
	if ( !entry.isObject() ) return std::unexpected( "URL declarations must be objects" );

	if ( !entry[ "url" ].isString() || entry[ "url" ].asString().empty() )
		return std::unexpected( "URL declarations require a non-empty url string" );

	if ( !entry.isMember( "type" ) ) return std::nullopt;

	if ( !entry[ "type" ].isString() ) return std::unexpected( "URL type must be a string" );

	const std::string type { entry[ "type" ].asString() };
	if ( type != "file" && type != "post" && type != "gallery" && type != "unknown" )
		return std::unexpected( "URL type must be file, post, gallery, or unknown" );

	return std::optional { UrlTypeDeclaration { entry[ "url" ].asString(), type } };
}

static std::expected< std::vector< UrlTypeDeclaration >, std::string > importUrlTypes( const Json::Value& options )
{
	const auto& urls { options[ "urls" ] };
	if ( urls.isNull() ) return std::vector< UrlTypeDeclaration > {};

	if ( !urls.isArray() ) return std::unexpected( "idhan.import urls must be an array" );

	std::vector< UrlTypeDeclaration > declarations {};
	for ( const auto& entry : urls )
	{
		auto declaration { urlTypeDeclaration( entry ) };
		if ( !declaration ) return std::unexpected( declaration.error() );

		if ( *declaration ) declarations.emplace_back( std::move( **declaration ) );
	}
	return declarations;
}

static std::expected< void, std::string > prepareImportTags( Json::Value& options )
{
	if ( options.isMember( "tagDomain" ) )
		return std::unexpected( "idhan.import tagDomain is no longer supported; use tags: {domain: [tags]}" );

	const auto& tags { options[ "tags" ] };
	if ( tags.isNull() ) return {};

	if ( tags.isArray() )
	{
		for ( const auto& tag : tags )
		{
			if ( !tag.isString() ) return std::unexpected( "idhan.import tags must contain only strings" );
		}

		Json::Value grouped { Json::objectValue };
		grouped[ "downloader tags" ] = tags;
		options[ "tags" ] = std::move( grouped );
		return {};
	}

	if ( !tags.isObject() )
		return std::unexpected( "idhan.import tags must be an array or an object mapping domains to tag arrays" );

	for ( const auto& domain : tags.getMemberNames() )
	{
		if ( domain.find_first_not_of( " \t\r\n\f\v" ) == std::string::npos )
			return std::unexpected( "idhan.import tags domain names must be non-empty strings" );

		if ( domain.contains( '\0' ) )
			return std::unexpected( "idhan.import tags domain names must not contain NUL characters" );

		const auto& entries { tags[ domain ] };
		if ( !entries.isArray() )
			return std::unexpected( format_ns::format( "idhan.import tags for domain '{}' must be an array", domain ) );

		for ( const auto& tag : entries )
		{
			if ( !tag.isString() )
				return std::unexpected(
					format_ns::format( "idhan.import tags for domain '{}' must contain only strings", domain ) );
		}
	}

	return {};
}

static JSValue delayBinding( JSContext* context, JSValueConst, const int count, JSValueConst* arguments )
{
	std::uint32_t milliseconds {};
	if ( count == 0 || JS_ToUint32( context, &milliseconds, arguments[ 0 ] ) != 0 || milliseconds > 300000 )
		return JS_ThrowTypeError( context, "delay requires milliseconds between 0 and 300000" );

	auto* execution { executionFor( context ) };
	if ( !execution || !execution->session ) return JS_ThrowInternalError( context, "No download session" );

	PendingPromise promise;
	JSValue result { makePromise( context, promise ) };
	if ( JS_IsException( result ) ) return result;

	execution->session->startDelay( *execution, milliseconds, promise );
	return result;
}

static JSValue importDirectBinding( JSContext* context, JSValueConst, const int count, JSValueConst* arguments )
{
	try
	{
		if ( count == 0 || !JS_IsObject( arguments[ 0 ] ) )
			return JS_ThrowTypeError( context, "importDirect expects options" );

		auto options_e { scriptValueToJson( context, arguments[ 0 ] ) };
		if ( !options_e.has_value() ) return JS_ThrowTypeError( context, "%s", options_e.error().c_str() );

		auto& options { options_e.value() };
		if ( !options[ "id" ].isUInt64() || !options[ "operation" ].isString() )
			return JS_ThrowTypeError( context, "Invalid direct import operation" );

		std::string chunk;
		if ( count > 1 )
		{
			if ( JS_IsString( arguments[ 1 ] ) )
				return JS_ThrowTypeError( context, "Direct imports require binary chunks" );

			JSValueHandle wrapper { context, JS_NewObject( context ) };
			JS_SetPropertyStr( context, wrapper.get(), "body", JS_DupValue( context, arguments[ 1 ] ) );
			const auto bytes_e { requestBody( context, wrapper.get() ) };
			if ( !bytes_e.has_value() ) return JS_ThrowTypeError( context, "%s", bytes_e.error().c_str() );

			if ( !bytes_e.value().has_value() ) return JS_ThrowTypeError( context, "Missing binary chunk" );

			chunk = bytes_e.value().value();
			if ( chunk.size() > 1024 * 1024 )
				return JS_ThrowTypeError( context, "Direct import chunks must not exceed 1 MiB" );
		}
		if ( options[ "operation" ] == "open" )
		{
			const auto tags_e { prepareImportTags( options ) };
			if ( !tags_e.has_value() ) return JS_ThrowTypeError( context, "%s", tags_e.error().c_str() );

			const auto urls_e { importUrlTypes( options ) };
			if ( !urls_e.has_value() ) return JS_ThrowTypeError( context, "%s", urls_e.error().c_str() );
		}
		auto* execution { executionFor( context ) };
		if ( !execution || !execution->session ) return JS_ThrowInternalError( context, "No download session" );

		PendingPromise promise;
		JSValue result { makePromise( context, promise ) };
		if ( JS_IsException( result ) ) return result;

		execution->session->startImportDirect( *execution, std::move( options ), std::move( chunk ), promise );
		return result;
	}
	IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )
}

struct ImportHandle
{
	std::shared_ptr< ImportStatus > status;
	JSValue promise { JS_UNDEFINED };
};

static JSClassID import_handle_class_id {};
static std::once_flag import_handle_class_once;

static void freeImportHandle( JSRuntime* runtime, JSValue value )
{
	auto* handle { static_cast< ImportHandle* >( JS_GetOpaque( value, import_handle_class_id ) ) };
	if ( handle == nullptr ) return;

	JS_FreeValueRT( runtime, handle->promise );
	delete handle;
}

static void markImportHandle( JSRuntime* runtime, JSValueConst value, JS_MarkFunc* mark )
{
	const auto* handle { static_cast< ImportHandle* >( JS_GetOpaque( value, import_handle_class_id ) ) };
	if ( handle == nullptr ) return;

	JS_MarkValue( runtime, handle->promise, mark );
}

static JSValue importFinishedBinding( JSContext* context, JSValueConst value )
{
	try
	{
		auto* handle { static_cast< ImportHandle* >( JS_GetOpaque2( context, value, import_handle_class_id ) ) };
		if ( handle == nullptr ) return JS_EXCEPTION;

		if ( !JS_IsUndefined( handle->promise ) ) return JS_DupValue( context, handle->promise );

		auto* execution { executionFor( context ) };
		if ( execution == nullptr || execution->session == nullptr )
			return JS_ThrowInternalError( context, "Import status requires an active download session" );

		PendingPromise promise;
		JSValue result { makePromise( context, promise ) };
		if ( JS_IsException( result ) ) return result;

		handle->promise = JS_DupValue( context, result );
		execution->session->awaitImport( *execution, handle->status, promise );
		return result;
	}
	IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )
}

static bool installImportHandle( JSContext* context )
{
	std::call_once( import_handle_class_once, [] { JS_NewClassID( &import_handle_class_id ); } );
	auto* runtime { JS_GetRuntime( context ) };
	const JSClassDef definition {
		.class_name = "ImportHandle", .finalizer = freeImportHandle, .gc_mark = markImportHandle
	};
	if ( !JS_IsRegisteredClass( runtime, import_handle_class_id ) )
	{
		if ( JS_NewClass( runtime, import_handle_class_id, &definition ) < 0 ) return false;
	}

	JSValueHandle prototype { context, JS_NewObject( context ) };
	if ( JS_IsException( prototype.get() ) ) return false;

	const auto completion { quickjs::property( "importFinished", importFinishedBinding, nullptr ) };
	if ( JS_SetPropertyFunctionList( context, prototype.get(), &completion, 1 ) < 0 ) return false;

	JS_SetClassProto( context, import_handle_class_id, prototype.release() );
	return true;
}

static JSValue queueImportBinding( JSContext* context, JSValueConst, const int count, JSValueConst* arguments )
{
	try
	{
		if ( count == 0 || !JS_IsObject( arguments[ 0 ] ) )
			return JS_ThrowTypeError( context, "idhan.queueImport expects an options object" );

		auto options_e { scriptValueToJson( context, arguments[ 0 ] ) };

		if ( !options_e.has_value() ) return JS_ThrowTypeError( context, "%s", options_e.error().c_str() );

		auto& options { options_e.value() };
		const auto& request { options[ "request" ] };

		if ( !request.isObject() || !request[ "url" ].isString() || request[ "url" ].asString().empty() )
			return JS_ThrowTypeError( context, "idhan.queueImport requires request.url" );

		const auto declarations { importUrlTypes( options ) };
		if ( !declarations ) return JS_ThrowTypeError( context, "%s", declarations.error().c_str() );

		auto* execution { executionFor( context ) };

		if ( execution == nullptr || execution->session == nullptr )
			return JS_ThrowInternalError( context, "idhan.queueImport has no download session" );

		const auto tags_e { prepareImportTags( options ) };
		if ( !tags_e.has_value() ) return JS_ThrowTypeError( context, "%s", tags_e.error().c_str() );

		applyDefaultReferer( options[ "request" ], execution->parser_url );

		JSValueHandle result { context, JS_NewObjectClass( context, import_handle_class_id ) };
		if ( JS_IsException( result.get() ) ) return result.release();

		auto handle { std::make_unique< ImportHandle >() };
		handle->status = execution->session->startImport( *execution, std::move( options ) );
		JS_SetOpaque( result.get(), handle.release() );
		return result.release();
	}
	IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )
}

static JSValue setUrlTypeBinding( JSContext* context, JSValueConst, const int count, JSValueConst* arguments )
{
	try
	{
		if ( count == 0 || !JS_IsObject( arguments[ 0 ] ) )
			return JS_ThrowTypeError( context, "idhan.setUrlType expects an options object" );

		const auto options { scriptValueToJson( context, arguments[ 0 ] ) };
		if ( !options ) return JS_ThrowTypeError( context, "%s", options.error().c_str() );

		const auto declaration { urlTypeDeclaration( *options ) };
		if ( !declaration ) return JS_ThrowTypeError( context, "%s", declaration.error().c_str() );

		if ( !*declaration ) return JS_ThrowTypeError( context, "idhan.setUrlType requires a type" );

		auto* execution { executionFor( context ) };
		if ( execution == nullptr || execution->session == nullptr )
			return JS_ThrowInternalError( context, "idhan.setUrlType has no download session" );

		const auto& [ url, type ] { **declaration };
		if ( !ada::parse< ada::url_aggregator >( url ).has_value() )
			return JS_ThrowTypeError( context, "URL type declarations require a valid absolute URL" );

		PendingPromise promise;
		JSValue value { makePromise( context, promise ) };
		if ( JS_IsException( value ) ) return value;

		execution->session->startSetUrlType( *execution, url, type, promise );
		return value;
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

		auto options { scriptValueToJson( context, arguments[ 0 ] ) };
		if ( !options ) return JS_ThrowTypeError( context, "%s", options.error().c_str() );

		const auto tags_e { prepareImportTags( options.value() ) };
		if ( !tags_e.has_value() )
			return JS_ThrowTypeError( context, "idhan.follow received invalid tags: %s", tags_e.error().c_str() );

		const auto declaration { urlTypeDeclaration( *options ) };
		if ( !declaration ) return JS_ThrowTypeError( context, "%s", declaration.error().c_str() );

		auto* execution { executionFor( context ) };

		if ( execution == nullptr || execution->session == nullptr )
			return JS_ThrowInternalError( context, "idhan.follow has no download session" );

		std::optional< std::string > type;
		if ( declaration.value().has_value() ) type = declaration.value().value().second;

		if ( type.has_value() && !ada::parse< ada::url_aggregator >( url.value() ).has_value() )
			return JS_ThrowTypeError( context, "URL type declarations require a valid absolute URL" );

		PendingPromise promise;
		JSValue value { makePromise( context, promise ) };
		if ( JS_IsException( value ) ) return value;

		execution->session->startFollow( *execution, url.value(), std::move( type ), ( *options )[ "tags" ], promise );
		return value;
	}
	IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )
}

static JSValue secretsBinding( JSContext* context, JSValueConst, const int count, JSValueConst* )
{
	try
	{
		if ( count != 0 ) return JS_ThrowTypeError( context, "idhan.secrets takes no arguments" );

		auto* execution { executionFor( context ) };

		if ( execution == nullptr || execution->session == nullptr )
			return JS_ThrowInternalError( context, "idhan.secrets has no download session" );

		const auto site_e { ada::parse< ada::url_aggregator >( execution->parser_url ) };

		if ( !site_e.has_value() ) return JS_ThrowTypeError( context, "Invalid secret scope URL" );

		const auto& site { site_e.value() };
		const auto domain_e { secretDomain(
			execution->route.configured_domain.empty() ?
				std::string { site.get_hostname() } :
				execution->route.configured_domain ) };

		if ( !domain_e.has_value() ) return JS_ThrowTypeError( context, "%s", domain_e.error().c_str() );

		const auto value_e { execution->session->secrets( domain_e.value() ) };

		if ( !value_e.has_value() ) return JS_ThrowInternalError( context, "%s", value_e.error().c_str() );

		const auto& value { value_e.value() };

		if ( !value.isObject() ) return JS_ThrowTypeError( context, "Domain secrets must be a JSON object" );

		Json::StreamWriterBuilder writer;
		writer[ "indentation" ] = "";
		const auto json { Json::writeString( writer, value ) };
		return JS_ParseJSON( context, json.data(), json.size(), "domain-secrets" );
	}
	IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )
}

static JSValue setCookieBinding( JSContext* context, JSValueConst, const int count, JSValueConst* arguments )
{
	try
	{
		if ( count < 2 ) return JS_ThrowTypeError( context, "setCookie requires name and value" );

		const auto name { scriptValueString( context, arguments[ 0 ] ) };
		const auto value { scriptValueString( context, arguments[ 1 ] ) };
		auto* execution { executionFor( context ) };

		if ( execution == nullptr || execution->session == nullptr )
			return JS_ThrowInternalError( context, "setCookie has no session" );

		const auto saved_e { execution->session->setCookie( *execution, name, value ) };

		if ( !saved_e.has_value() ) return JS_ThrowTypeError( context, "%s", saved_e.error().c_str() );

		return JS_UNDEFINED;
	}
	IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )
}

static JSValue learnHostBinding( JSContext* context, JSValueConst, const int count, JSValueConst* arguments )
{
	try
	{
		if ( count < 2 ) return JS_ThrowTypeError( context, "learnHost requires an origin and a URL class" );

		const auto origin { scriptValueString( context, arguments[ 0 ] ) };
		const auto url_class { scriptValueString( context, arguments[ 1 ] ) };
		auto* execution { executionFor( context ) };

		if ( execution == nullptr || execution->session == nullptr )
			return JS_ThrowInternalError( context, "learnHost has no session" );

		PendingPromise promise;
		JSValue value { makePromise( context, promise ) };
		if ( JS_IsException( value ) ) return value;

		execution->session->startLearnHost( *execution, origin, url_class, promise );
		return value;
	}
	IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )
}

static JSValue parserStateBinding( JSContext* context, JSValueConst, const int count, JSValueConst* arguments )
{
	try
	{
		if ( count < 3 )
			return JS_ThrowTypeError( context, "parserState requires a parser ID, origin, and secrets snapshot" );

		auto* execution { executionFor( context ) };

		if ( execution == nullptr || execution->session == nullptr )
			return JS_ThrowInternalError( context, "parserState has no download session" );

		auto secrets_e { scriptValueToJson( context, arguments[ 2 ] ) };
		if ( !secrets_e.has_value() ) return JS_ThrowTypeError( context, "Invalid parser secrets snapshot" );

		auto& secrets { secrets_e.value() };
		if ( !secrets.isObject() ) return JS_ThrowTypeError( context, "Parser secrets snapshot must be an object" );

		auto* scripts { static_cast< ScriptContext* >( JS_GetRuntimeOpaque( JS_GetRuntime( context ) ) ) };
		return scripts->parserState(
			*execution,
			scriptValueString( context, arguments[ 0 ] ),
			scriptValueString( context, arguments[ 1 ] ),
			std::move( secrets ) );
	}
	IDHAN_QJS_TRANSLATE_EXCEPTIONS( context )
}

static JSValue keyValueBinding( JSContext* context, JSValueConst, const int count, JSValueConst* arguments )
{
	try
	{
		if ( count < 3 ) return JS_ThrowTypeError( context, "kv requires operation, parser and key" );

		for ( int index {}; index < 3; ++index )
		{
			if ( !JS_IsString( arguments[ index ] ) )
				return JS_ThrowTypeError( context, "KV keys and scope must be strings" );
		}

		const auto operation { scriptValueString( context, arguments[ 0 ] ) };
		const auto parser { scriptValueString( context, arguments[ 1 ] ) };
		const auto key { scriptValueString( context, arguments[ 2 ] ) };
		auto* scripts { static_cast< ScriptContext* >( JS_GetRuntimeOpaque( JS_GetRuntime( context ) ) ) };
		auto& store { scripts->keyValues() };
		if ( operation == "get" )
		{
			const auto json { store.get( parser, key ) };
			if ( !json.has_value() ) return JS_UNDEFINED;

			return JS_ParseJSON( context, json->data(), json->size(), "parser-kv" );
		}

		if ( operation == "delete" ) return JS_NewBool( context, store.erase( parser, key ) );

		if ( operation != "set" || count < 4 ) return JS_ThrowTypeError( context, "Invalid KV operation" );

		double seconds { 86400 };
		if ( count >= 5 && !JS_IsUndefined( arguments[ 4 ] ) )
		{
			if ( !JS_IsNumber( arguments[ 4 ] ) )
				return JS_ThrowTypeError( context, "KV lifetime must be a number of seconds" );

			if ( JS_ToFloat64( context, &seconds, arguments[ 4 ] ) < 0 ) return JS_EXCEPTION;
		}

		// Bound conversion to signed seconds and steady_clock's representable range.
		if ( !std::isfinite( seconds ) || seconds < 1 || std::trunc( seconds ) != seconds
		     || seconds > 9007199254740991.0 )
			return JS_ThrowRangeError( context, "KV lifetime must be a positive whole number of seconds" );

		auto value_e { scriptValueToJson( context, arguments[ 3 ] ) };
		if ( !value_e.has_value() ) return JS_ThrowTypeError( context, "%s", value_e.error().c_str() );

		Json::StreamWriterBuilder writer;
		writer[ "indentation" ] = "";
		store.set(
			parser,
			key,
			Json::writeString( writer, value_e.value() ),
			std::chrono::seconds { static_cast< std::int64_t >( seconds ) } );
		return JS_UNDEFINED;
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
	if ( !installImportHandle( context ) ) return std::unexpected( "Unable to install import handles" );

	JSValue global { JS_GetGlobalObject( context ) };
	JSValue idhan { JS_NewObject( context ) };

	bool success { setFunction( context, idhan, "request", requestBinding, 1 ) };
	success = setFunction( context, idhan, "queueImport", queueImportBinding, 1 ) && success;
	success = setFunction( context, idhan, "delay", delayBinding, 1 ) && success;
	success = setFunction( context, idhan, "importDirect", importDirectBinding, 2 ) && success;
	success = setFunction( context, idhan, "follow", followBinding, 1 ) && success;
	success = setFunction( context, idhan, "setUrlType", setUrlTypeBinding, 1 ) && success;
	success = setFunction( context, idhan, "secrets", secretsBinding, 0 ) && success;
	success = setFunction( context, idhan, "setCookie", setCookieBinding, 2 ) && success;
	success = setFunction( context, idhan, "learnHost", learnHostBinding, 2 ) && success;
	success = setFunction( context, idhan, "parserState", parserStateBinding, 3 ) && success;
	success = setFunction( context, idhan, "kv", keyValueBinding, 5 ) && success;
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

	const auto url_pattern_e { installURLPatternBindings( context ) };

	if ( !url_pattern_e.has_value() )
	{
		return std::unexpected( format_ns::format( "Unable to install URLPattern: {}", url_pattern_e.error() ) );
	}

	if ( !quickjs::installHTMLBindings( context ) ) return std::unexpected( "Unable to install the HTML bindings" );
	if ( const auto fetch { installFetchBindings( context ) }; !fetch )
		return std::unexpected( format_ns::format( "Unable to install the fetch bindings: {}", fetch.error() ) );

	const auto crypto_e { installWebCryptoBindings( context ) };
	if ( !crypto_e.has_value() ) return std::unexpected( crypto_e.error() );

	return {};
}

} // namespace idhan::downloader
