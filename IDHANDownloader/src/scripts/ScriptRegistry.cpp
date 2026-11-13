#include "scripts/ScriptRegistry.hpp"

#include <folly/ScopeGuard.h>
#include <json/json.h>
#include <spdlog/spdlog.h>

#include <ada.h>
#include <algorithm>
#include <fstream>
#include <map>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#include "IDHANDownloader/urls/UrlNormalization.hpp"
#include "urls/URLUtils.hpp"
#include "js/JSValueHandle.hpp"
#include "js/ScriptContext.hpp"
#include "js/ScriptExecution.hpp"
#include "js/bindings/IdhanBindings.hpp"
#include "logging/format_ns.hpp"
#include "scripts/ParserSDKVersion.hpp"
#include "scripts/URLPatternMatcher.hpp"

namespace idhan::downloader
{

static std::expected< std::string, std::string > requiredString(
	const Json::Value& object,
	const char* field,
	const std::string_view owner )
{
	const auto& value { object[ field ] };

	if ( !value.isString() || value.asString().empty() )
		return std::unexpected( format_ns::format( "{} requires a non-empty '{}' string", owner, field ) );

	return value.asString();
}

static std::expected< std::optional< std::string >, std::string > optionalString(
	const Json::Value& object,
	const char* field,
	const std::string_view owner )
{
	const auto& value { object[ field ] };

	if ( value.isNull() ) return std::nullopt;

	if ( !value.isString() )
		return std::unexpected( format_ns::format( "{} field '{}' must be a string", owner, field ) );

	return value.asString();
}

static std::expected< std::vector< std::string >, std::string > queryNames(
	const Json::Value& object,
	const char* field,
	const std::string_view owner )
{
	std::vector< std::string > names {};
	if ( !object.isMember( field ) ) return names;

	const auto& value { object[ field ] };
	if ( !value.isArray() ) return std::unexpected( format_ns::format( "{} {} must be an array", owner, field ) );

	for ( const auto& name : value )
	{
		if ( !name.isString() || name.asString().empty() )
			return std::unexpected( format_ns::format( "{} {} requires non-empty strings", owner, field ) );

		names.emplace_back( name.asString() );
	}

	return names;
}

static void appendNames( std::vector< std::string >& names, const std::vector< std::string >& added )
{
	names.insert( names.end(), added.begin(), added.end() );
}

struct RateDefinition
{
	bool defined {};
	std::optional< RequestRate > rate {};
	bool share_subdomains {};
	bool pool_subdomains {};
};

static std::expected< RateDefinition, std::string > requestRate(
	const Json::Value& object,
	const std::string_view owner )
{
	const auto& legacy_value { object[ "requestRate" ] };
	const auto& new_value { object[ "rateLimit" ] };

	const auto& value { new_value.isNull() ? legacy_value : new_value };

	if ( value.isNull() ) return RateDefinition {};

	if ( value.isBool() )
	{
		if ( value.asBool() )
			return std::unexpected( format_ns::format( "{} requestRate must be an object or false", owner ) );

		return RateDefinition { .defined = true };
	}

	if ( !value.isObject() )
		return std::unexpected( format_ns::format( "{} requestRate must be an object or false", owner ) );

	const auto& requests { value[ "requests" ] };
	const auto& seconds { value[ "seconds" ] };
	const auto& spread { value[ "spread" ] };
	const auto& concurrency { value[ "concurrency" ] };

	if ( !concurrency.isNull() )
	{
		if ( !concurrency.isUInt64() )
			return std::unexpected(
				format_ns::format( "{} requestRate concurrency must be a positive integer", owner ) );

		if ( concurrency.asUInt64() == 0 )
			return std::unexpected(
				format_ns::format( "{} requestRate concurrency must be a positive integer", owner ) );

		if ( concurrency.asUInt64() > ( std::numeric_limits< std::size_t >::max )() )
			return std::unexpected( format_ns::format( "{} requestRate concurrency is too large", owner ) );
	}

	if ( !spread.isNull() && !spread.isBool() )
		return std::unexpected( format_ns::format( "{} requestRate spread must be a boolean", owner ) );

	if ( !requests.isUInt64() || !seconds.isUInt64() || requests.asUInt64() == 0 || seconds.asUInt64() == 0 )
		return std::unexpected(
			format_ns::format( "{} requestRate requires positive integer requests and seconds", owner ) );

	const auto& share_subdomains { value[ "shareSubdomains" ] };
	const auto& pool_subdomains { value[ "poolSubdomains" ] };

	if ( !share_subdomains.isNull() && !share_subdomains.isBool() )
		return std::unexpected( format_ns::format( "{} requestRate shareSubdomains must be a boolean", owner ) );

	if ( !pool_subdomains.isNull() && !pool_subdomains.isBool() )
		return std::unexpected( format_ns::format( "{} requestRate poolSubdomains must be a boolean", owner ) );

	if ( pool_subdomains.asBool() && !share_subdomains.asBool() )
		return std::unexpected( format_ns::format( "{} requestRate poolSubdomains requires shareSubdomains", owner ) );

	return RateDefinition {
		.defined = true,
		.rate =
			RequestRate {
				.requests = requests.asUInt64(),
				.seconds = seconds.asUInt64(),
				.spread = spread.asBool(),
				.concurrency = concurrency.isNull() ? 1 : static_cast< std::size_t >( concurrency.asUInt64() ) },
		.share_subdomains = share_subdomains.asBool(),
		.pool_subdomains = pool_subdomains.asBool(),
	};
}

static std::expected< std::optional< HttpVersion >, std::string > httpVersion(
	const Json::Value& value,
	const std::string_view owner )
{
	if ( value.isNull() ) return std::nullopt;

	if ( !value.isString() )
		return std::unexpected( format_ns::format( "{} lane httpVersion must be \"1.1\", \"2\" or \"3\"", owner ) );

	const std::string text { value.asString() };

	if ( text == "1.1" ) return HttpVersion::HTTP_1_1;
	if ( text == "2" ) return HttpVersion::HTTP_2;
	if ( text == "3" ) return HttpVersion::HTTP_3;

	return std::unexpected( format_ns::format( "{} lane httpVersion must be \"1.1\", \"2\" or \"3\"", owner ) );
}

static std::expected< std::unordered_map< std::int32_t, StatusRetry >, std::string > statusRetries(
	const Json::Value& value,
	const std::string_view owner )
{
	if ( !value.isArray() ) return std::unexpected( format_ns::format( "{} retry must be an array", owner ) );

	std::unordered_map< std::int32_t, StatusRetry > retries {};
	for ( const auto& entry : value )
	{
		if ( !entry.isObject() )
			return std::unexpected( format_ns::format( "{} retry entries must be objects", owner ) );

		const auto& code { entry[ "code" ] };
		if ( !code.isInt() )
			return std::unexpected( format_ns::format( "{} retry code must be an HTTP error status", owner ) );

		const auto status { code.asInt() };
		if ( status < 400 || status > 599 )
			return std::unexpected( format_ns::format( "{} retry code must be an HTTP error status", owner ) );

		const auto& count { entry[ "count" ] };
		if ( !count.isUInt() )
			return std::unexpected( format_ns::format( "{} retry count must be a non-negative integer", owner ) );

		const auto& wait { entry[ "wait" ] };
		if ( !wait.isNull() && !wait.isUInt() )
			return std::unexpected( format_ns::format( "{} retry wait must be a number of seconds", owner ) );

		const StatusRetry retry { .count = count.asUInt(), .wait = std::chrono::seconds { wait.asUInt() } };

		if ( !retries.emplace( status, retry ).second )
			return std::unexpected( format_ns::format( "{} retry lists HTTP status {} twice", owner, status ) );
	}

	return retries;
}

//! Reads optional retry and lane settings, leaving unmentioned settings at their inherited values.
static std::expected< void, std::string > laneOverrides(
	const Json::Value& object,
	const std::string_view owner,
	LaneSettings& settings )
{
	if ( object.isMember( "retry" ) )
	{
		auto retries_e { statusRetries( object[ "retry" ], owner ) };
		if ( !retries_e.has_value() ) return std::unexpected( retries_e.error() );

		settings.status_retries = std::move( retries_e.value() );
	}

	const auto& value { object[ "lane" ] };

	if ( value.isNull() ) return {};

	if ( !value.isObject() ) return std::unexpected( format_ns::format( "{} lane must be an object", owner ) );

	if ( value.isMember( "backoffStatuses" ) )
	{
		const auto& statuses { value[ "backoffStatuses" ] };
		if ( !statuses.isArray() )
			return std::unexpected( format_ns::format( "{} lane backoffStatuses must be an array", owner ) );

		std::unordered_set< std::int32_t > configured {};
		for ( const auto& status : statuses )
		{
			if ( !status.isInt() )
				return std::unexpected(
					format_ns::format( "{} lane backoffStatuses must contain HTTP error statuses", owner ) );

			const auto code { status.asInt() };
			if ( code < 400 || code > 599 )
				return std::unexpected(
					format_ns::format( "{} lane backoffStatuses must contain HTTP error statuses", owner ) );

			configured.emplace( code );
		}

		settings.backoff_statuses = std::move( configured );
	}

	if ( const auto& keep_alive { value[ "keepAlive" ] }; !keep_alive.isNull() )
	{
		if ( !keep_alive.isUInt64() )
			return std::unexpected( format_ns::format( "{} lane keepAlive must be a number of seconds", owner ) );

		settings.keep_alive = std::chrono::seconds { keep_alive.asUInt64() };
	}

	if ( const auto& backoff { value[ "errorBackoff" ] }; !backoff.isNull() )
	{
		if ( !backoff.isUInt64() )
			return std::unexpected( format_ns::format( "{} lane errorBackoff must be a number of seconds", owner ) );

		settings.error_backoff = std::chrono::seconds { backoff.asUInt64() };
	}

	if ( const auto& concurrency { value[ "concurrency" ] }; !concurrency.isNull() )
	{
		if ( !concurrency.isUInt64() )
			return std::unexpected( format_ns::format( "{} lane concurrency must be a positive integer", owner ) );

		settings.concurrency = concurrency.asUInt64();
	}

	auto version { httpVersion( value[ "httpVersion" ], owner ) };

	if ( !version ) return std::unexpected( std::move( version.error() ) );

	if ( version->has_value() ) settings.http_version = *version;

	auto group { optionalString( value, "group", owner ) };

	if ( !group ) return std::unexpected( std::move( group.error() ) );

	if ( group->has_value() ) settings.group = std::move( *group );

	return {};
}

std::expected< Json::Value, std::string > normalizeURLClass(
	const Json::Value& definition,
	const std::filesystem::path& parser_directory )
{
	if ( !definition.isObject() ) return std::unexpected( "URL classes must be objects" );

	if ( !definition.isMember( "id" ) ) return definition;

	if ( definition[ "id" ].isString() && !definition.isMember( "parser" ) )
	{
		Json::Value policy { definition };
		policy[ "name" ] = definition[ "id" ];
		policy[ "script" ] = "";
		policy[ "routes" ] = Json::Value { Json::arrayValue };
		return policy;
	}

	if ( !definition[ "id" ].isString() || !definition[ "parser" ].isString() || !definition[ "url_patterns" ].isArray()
	     || !definition[ "generators" ].isArray() )
		return std::unexpected( "URL classes require id, parser, url_patterns and generators" );

	Json::Value converted { definition };
	converted[ "name" ] = definition[ "id" ];
	converted[ "script" ] = ( parser_directory / ( definition[ "parser" ].asString() + ".js" ) ).string();
	converted[ "routes" ] = Json::Value { Json::arrayValue };

	for ( const auto& rule : definition[ "url_patterns" ] )
	{
		if ( !rule.isObject() || !rule[ "generator" ].isString() || !rule[ "patterns" ].isArray() )
			return std::unexpected( "URL rules require generator and patterns" );

		std::string handler;

		for ( const auto& generator : definition[ "generators" ] )
		{
			if ( !generator.isObject() ) return std::unexpected( "Generators must be objects" );

			if ( generator[ "id" ] == rule[ "generator" ] && generator[ "handler" ].isString() )
				handler = generator[ "handler" ].asString();
		}

		if ( handler.empty() ) return std::unexpected( "URL rule references an undeclared generator" );

		for ( const auto& pattern : rule[ "patterns" ] )
		{
			if ( !pattern.isObject() ) return std::unexpected( "URL patterns must be objects" );

			Json::Value route { Json::objectValue };
			route[ "export" ] = handler;
			route[ "urlPattern" ] = pattern;
			if ( rule.isMember( "removeQuery" ) ) route[ "removeQuery" ] = rule[ "removeQuery" ];

			converted[ "routes" ].append( std::move( route ) );
		}
	}

	return converted;
}

std::expected< Json::Value, std::string > loadParserMetadata(
	BytecodeCache& bytecode,
	const std::filesystem::path& path )
{
	ScriptContext scripts { {}, bytecode };
	ScriptExecution execution;
	execution.route.script = path;
	const auto context_e { scripts.createRealm( execution ) };

	if ( !context_e.has_value() ) return std::unexpected( context_e.error() );

	JSContextPtr context { context_e.value() };
	execution.context = context.get();
	const auto evaluated_e { scripts.evaluate( execution, path ) };

	if ( !evaluated_e.has_value() ) return std::unexpected( evaluated_e.error() );

	JSValueHandle evaluated { context.get(), evaluated_e.value() };
	std::string error;

	if ( !scripts.runReadyMicrotasks( error ) ) return std::unexpected( error );

	if ( JS_PromiseState( context.get(), evaluated.get() ) == JS_PROMISE_PENDING )
		return std::unexpected( "Parser metadata must be available without asynchronous module work" );

	if ( JS_PromiseState( context.get(), evaluated.get() ) == JS_PROMISE_REJECTED )
	{
		JSValueHandle reason { context.get(), JS_PromiseResult( context.get(), evaluated.get() ) };
		return std::unexpected( scriptErrorString( context.get(), reason.get() ) );
	}

	// Metadata getters and JSON serialization can execute parser code too.
	const ScriptContext::ExecutionScope timing { context.get() };
	JSValueHandle module { context.get(), JS_GetModuleNamespace( context.get(), execution.module ) };
	JSValueHandle constructor { context.get(), JS_GetPropertyStr( context.get(), module.get(), "default" ) };
	const bool policy_only { !JS_IsFunction( context.get(), constructor.get() ) };
	Json::Value result;

	if ( policy_only )
	{
		// A module without a parser class only contributes its exported URL class policies.
		JSValueHandle classes { context.get(), JS_GetPropertyStr( context.get(), module.get(), "urlClasses" ) };

		if ( !JS_IsObject( classes.get() ) )
			return std::unexpected( "JavaScript parsers must default-export a parser class" );

		auto classes_e { scriptValueToJson( context.get(), classes.get() ) };

		if ( !classes_e.has_value() ) return std::unexpected( classes_e.error() );

		result = Json::Value { Json::objectValue };
		result[ "id" ] = path.stem().string();
		result[ "version" ] = 1;
		result[ "urlClasses" ] = std::move( classes_e.value() );
		result[ "handlers" ] = Json::Value { Json::arrayValue };
		result[ "discoveries" ] = Json::Value { Json::arrayValue };
		result[ "options" ] = Json::Value { Json::arrayValue };
	}
	else
	{
		JSValueHandle metadata { context.get(), JS_GetPropertyStr( context.get(), constructor.get(), "metadata" ) };

		if ( !JS_IsObject( metadata.get() ) ) return std::unexpected( "Parser class has no decorated metadata" );

		auto metadata_e { scriptValueToJson( context.get(), metadata.get() ) };

		if ( !metadata_e.has_value() ) return std::unexpected( metadata_e.error() );

		result = std::move( metadata_e.value() );
	}

	if ( !result.isObject() ) return std::unexpected( "Parser metadata must be an object" );

	if ( !policy_only )
	{
		const auto& sdk_version { result[ "sdkVersion" ] };
		if ( !sdk_version.isString() )
			return std::unexpected(
				"Parser metadata requires an sdkVersion string; rebuild the parser with a supported SDK" );

		if ( !compatibleParserSDKVersion( sdk_version.asString() ) )
			return std::unexpected(
				format_ns::format(
					"Unsupported parser SDK version '{}'; requires major version {} and at least {}.{}.{}",
					sdk_version.asString(),
					required_sdk_major,
					required_sdk_major,
					required_sdk_minor,
					required_sdk_patch ) );
	}

	if ( !result[ "id" ].isString() || result[ "id" ].asString().empty() )
		return std::unexpected( "Parser metadata requires a non-empty id" );

	if ( !result[ "version" ].isUInt64() || result[ "version" ].asUInt64() == 0 )
		return std::unexpected( "Parser metadata requires a positive integer version" );

	for ( const auto* field : { "urlClasses", "handlers", "discoveries", "options" } )
	{
		if ( !result[ field ].isArray() )
			return std::unexpected( std::string { "Parser metadata requires an array: " } + field );
	}

	for ( const auto& definition : result[ "urlClasses" ] )
	{
		const auto normalized_e { normalizeURLClass( definition, bytecode.root() ) };

		if ( !normalized_e.has_value() ) return std::unexpected( normalized_e.error() );
	}

	if ( policy_only ) return result;

	JSValueHandle run { context.get(), JS_GetPropertyStr( context.get(), module.get(), "run" ) };

	if ( !JS_IsFunction( context.get(), run.get() ) )
		return std::unexpected( "Parser module must export its run adapter" );

	return result;
}

ScriptRegistry::ScriptRegistry( std::filesystem::path parser_directory, LaneSettings defaults ) :
  m_parser_directory( std::move( parser_directory ) ),
  m_bytecode( std::make_shared< BytecodeCache >( m_parser_directory ) ),
  m_defaults( std::move( defaults ) )
{}

std::expected< std::unique_ptr< ScriptRegistry >, std::string > ScriptRegistry::build(
	const std::filesystem::path& url_classes,
	std::filesystem::path parser_directory,
	LaneSettings defaults,
	Json::Value parsers,
	std::shared_ptr< BytecodeCache > bytecode )
{
	std::unique_ptr< ScriptRegistry > registry { new ScriptRegistry( parser_directory, std::move( defaults ) ) };
	registry->m_parsers = std::move( parsers );
	registry->m_bytecode = std::move( bytecode );
	for ( const auto& parser : registry->m_parsers )
	{
		const auto& discoveries { parser[ "metadata" ][ "discoveries" ] };
		if ( discoveries.isArray() && !discoveries.empty() ) registry->m_discoverers.append( parser[ "script" ] );
	}

	std::map< std::string, std::uint64_t > parser_versions;
	for ( const auto& parser : registry->m_parsers )
	{
		const auto& metadata { parser[ "metadata" ] };
		const auto id { metadata[ "id" ].asString() };
		const auto version { metadata[ "version" ].asUInt64() };
		if ( parser_versions.contains( id ) && parser_versions[ id ] >= version ) continue;

		parser_versions[ id ] = version;
		registry->m_parser_scripts[ id ] =
			( registry->m_parser_directory / parser[ "script" ].asString() ).lexically_normal();
	}

	Json::CharReaderBuilder reader {};
	std::vector< std::filesystem::path > files {};
	std::error_code filesystem_error {};
	if ( std::filesystem::is_directory( url_classes, filesystem_error ) )
	{
		for ( const auto& entry : std::filesystem::directory_iterator( url_classes, filesystem_error ) )
			if ( entry.is_regular_file() && entry.path().extension() == ".json" ) files.emplace_back( entry.path() );
		if ( filesystem_error )
			return std::unexpected(
				format_ns::format(
					"Unable to list URL Classes directory {}: {}", url_classes.string(), filesystem_error.message() ) );

		const auto private_directory { url_classes / "private" };
		std::error_code private_error {};
		const bool has_private { std::filesystem::is_directory( private_directory, private_error ) };

		if ( has_private )
			for ( const auto& entry : std::filesystem::directory_iterator( private_directory, filesystem_error ) )
				if ( entry.is_regular_file() && entry.path().extension() == ".json" )
					files.emplace_back( entry.path() );

		if ( filesystem_error )
			return std::unexpected(
				format_ns::format(
					"Unable to list URL Classes directory {}: {}", url_classes.string(), filesystem_error.message() ) );
		std::ranges::sort( files );
	}
	else if ( std::filesystem::is_regular_file( url_classes, filesystem_error ) )
		files.emplace_back( url_classes );

	Json::Value merged { Json::objectValue };
	merged[ "urlClasses" ] = Json::arrayValue;
	std::map< std::string, Json::ArrayIndex > class_indices {};
	for ( const auto& file : files )
	{
		std::ifstream input { file, std::ios::binary };
		if ( !input )
			return std::unexpected( format_ns::format( "Unable to read URL Classes file: {}", file.string() ) );
		Json::Value root {};
		std::string errors {};
		if ( !Json::parseFromStream( reader, input, &root, &errors ) )
			return std::unexpected(
				format_ns::format( "Unable to parse URL Classes file {}: {}", file.string(), errors ) );
		if ( root.isObject() && root.isMember( "id" ) )
		{
			Json::Value wrapped { Json::objectValue };
			wrapped[ "urlClasses" ] = Json::Value { Json::arrayValue };
			wrapped[ "urlClasses" ].append( std::move( root ) );
			root = std::move( wrapped );
		}

		const auto& classes { root[ "urlClasses" ] };
		if ( !root.isObject() || !classes.isArray() )
			return std::unexpected(
				format_ns::format( "URL Classes file must contain a 'urlClasses' array: {}", file.string() ) );
		for ( const auto& source : classes )
		{
			const auto definition_e { normalizeURLClass( source, std::filesystem::absolute( parser_directory ) ) };

			if ( !definition_e.has_value() ) return std::unexpected( definition_e.error() );

			Json::Value definition { definition_e.value() };

			if ( source[ "parser" ].isString() )
			{
				const Json::Value* selected {};
				for ( const auto& parser : registry->m_parsers )
				{
					const auto& metadata { parser[ "metadata" ] };
					if ( metadata[ "id" ] != source[ "parser" ] ) continue;

					if ( selected == nullptr
					     || metadata[ "version" ].asUInt64() > ( *selected )[ "metadata" ][ "version" ].asUInt64() )
						selected = &parser;
				}

				if ( selected == nullptr )
				{
					definition[ "script" ] = "";
					definition[ "routes" ] = Json::Value { Json::arrayValue };
				}
				else
				{
					definition[ "script" ] =
						( std::filesystem::absolute( parser_directory ) / ( *selected )[ "script" ].asString() )
							.string();
				}
			}
			if ( !definition.isObject() || !definition[ "name" ].isString() )
				return std::unexpected(
					format_ns::format( "URL Classes entries in {} require a name", file.string() ) );
			const auto name { definition[ "name" ].asString() };
			const auto found { class_indices.find( name ) };
			if ( found == class_indices.end() )
			{
				class_indices.emplace( name, merged[ "urlClasses" ].size() );
				merged[ "urlClasses" ].append( definition );
				continue;
			}
			Json::Value& existing { merged[ "urlClasses" ][ found->second ] };
			for ( const auto& key : definition.getMemberNames() )
			{
				if ( key == "name" ) continue;
				if ( key != "hosts" )
					return std::unexpected(
						format_ns::format(
							"URL Class '{}' may only add hosts in later files (field '{}', {})",
							name,
							key,
							file.string() ) );
				if ( !definition[ key ].isArray() )
					return std::unexpected(
						format_ns::format( "URL Class '{}' hosts must be an array in {}", name, file.string() ) );
				if ( !existing[ "hosts" ].isArray() ) existing[ "hosts" ] = Json::arrayValue;
				for ( const auto& host : definition[ key ] ) existing[ "hosts" ].append( host );
			}
		}
	}
	const std::size_t external_classes { merged[ "urlClasses" ].size() };

	if ( !files.empty() )
		spdlog::info( "Downloader URL classes: {} from {} external file(s)", external_classes, files.size() );

	for ( const auto& parser : registry->m_parsers )
	{
		const auto& bundled { parser[ "metadata" ][ "urlClasses" ] };

		if ( !bundled.isArray() ) continue;

		for ( const auto& source : bundled )
		{
			if ( class_indices.contains( source[ "id" ].asString() ) ) continue;

			auto definition_e { normalizeURLClass( source, std::filesystem::absolute( parser_directory ) ) };

			if ( !definition_e.has_value() ) return std::unexpected( definition_e.error() );

			auto& definition { definition_e.value() };

			if ( source[ "parser" ].isString() )
			{
				definition[ "script" ] =
					( std::filesystem::absolute( parser_directory ) / parser[ "script" ].asString() ).string();
			}

			class_indices.emplace( source[ "id" ].asString(), merged[ "urlClasses" ].size() );
			merged[ "urlClasses" ].append( std::move( definition ) );
		}
	}

	const Json::Value& classes { merged[ "urlClasses" ] };

	std::unordered_set< std::string > names {};
	const auto base_path {
		std::filesystem::absolute( url_classes.empty() ? parser_directory : url_classes.parent_path() )
	};

	for ( const auto& value : classes )
	{
		if ( !value.isObject() ) return std::unexpected( "URL Class entries must be objects" );

		auto name { requiredString( value, "name", "URL Class" ) };

		if ( !name ) return std::unexpected( std::move( name.error() ) );
		if ( !names.emplace( *name ).second )
			return std::unexpected( format_ns::format( "Duplicate URL Class name: {}", *name ) );

		const std::string owner { format_ns::format( "URL Class '{}'", *name ) };
		const auto script_e { optionalString( value, "script", owner ) };

		if ( !script_e.has_value() ) return std::unexpected( script_e.error() );

		const auto& hosts { value[ "hosts" ] };
		const auto& routes { value[ "routes" ] };

		if ( !hosts.isArray() ) return std::unexpected( format_ns::format( "{} requires a 'hosts' array", owner ) );

		if ( !routes.isArray() ) return std::unexpected( format_ns::format( "{} requires a 'routes' array", owner ) );

		URLClass url_class { .name = *name };
		const auto& script { script_e.value() };

		if ( script.has_value() && !script.value().empty() )
		{
			if ( routes.empty() ) return std::unexpected( format_ns::format( "{} requires non-empty routes", owner ) );

			url_class.script = ( base_path / script.value() ).lexically_normal();
			const auto resolved_e { registry->m_bytecode->resolve( url_class.script.string(), {} ) };

			if ( !resolved_e.has_value() ) return std::unexpected( resolved_e.error() );

			url_class.script = resolved_e.value();
		}
		else if ( !routes.empty() )
		{
			return std::unexpected( "URL class routes require an installed parser" );
		}

		if ( url_class.script.empty() && value.isMember( "url_patterns" ) )
		{
			const auto& rules { value[ "url_patterns" ] };
			if ( !rules.isArray() ) return std::unexpected( "URL class url_patterns must be an array" );

			for ( const auto& rule : rules )
			{
				if ( !rule.isObject() || !rule[ "patterns" ].isArray() )
					return std::unexpected( "URL rules require a patterns array" );

				auto remove_query_e { queryNames( rule, "removeQuery", owner ) };
				if ( !remove_query_e.has_value() ) return std::unexpected( remove_query_e.error() );

				for ( const auto& pattern : rule[ "patterns" ] )
				{
					const auto matcher_e { URLPatternMatcher::create( pattern ) };
					if ( !matcher_e.has_value() ) return std::unexpected( matcher_e.error() );

					url_class.page_patterns.emplace_back( matcher_e.value(), remove_query_e.value() );
				}
			}
		}

		if ( value.isMember( "sortQueryParameters" ) )
		{
			const auto& sort { value[ "sortQueryParameters" ] };
			if ( !sort.isBool() )
				return std::unexpected( format_ns::format( "{} sortQueryParameters must be a boolean", owner ) );

			url_class.sort_query_parameters = sort.asBool();
		}

		auto ephemeral_query_e { queryNames( value, "ephemeralQuery", owner ) };
		if ( !ephemeral_query_e.has_value() ) return std::unexpected( ephemeral_query_e.error() );

		url_class.ephemeral_query = std::move( ephemeral_query_e.value() );

		auto remove_query_e { queryNames( value, "removeQuery", owner ) };
		if ( !remove_query_e.has_value() ) return std::unexpected( remove_query_e.error() );

		url_class.remove_query = std::move( remove_query_e.value() );

		LaneSettings class_settings { registry->m_defaults };
		auto class_rate { requestRate( value, owner ) };

		if ( !class_rate ) return std::unexpected( std::move( class_rate.error() ) );
		if ( class_rate->defined ) class_settings.rate = class_rate->rate;

		if ( const auto applied { laneOverrides( value, owner, class_settings ) }; !applied )
			return std::unexpected( applied.error() );

		url_class.settings = class_settings;

		for ( const auto& host : hosts )
		{
			std::string hostname {};
			LaneSettings settings { class_settings };
			bool include_subdomains { class_rate->share_subdomains };
			bool pool_subdomains { class_rate->pool_subdomains };
			std::optional< std::string > points_to {};

			if ( host.isString() )
				hostname = host.asString();
			else if ( host.isObject() )
			{
				auto configured { requiredString( host, "host", "URL Class host" ) };

				if ( !configured ) return std::unexpected( std::move( configured.error() ) );

				hostname = std::move( *configured );
				const std::string host_owner { format_ns::format( "URL Class host '{}'", hostname ) };
				auto override_rate { requestRate( host, host_owner ) };

				if ( !override_rate ) return std::unexpected( std::move( override_rate.error() ) );

				if ( override_rate->defined )
				{
					settings.rate = override_rate->rate;
					include_subdomains = override_rate->share_subdomains;
					pool_subdomains = override_rate->pool_subdomains;
				}

				if ( const auto applied { laneOverrides( host, host_owner, settings ) }; !applied )
					return std::unexpected( applied.error() );

				auto points_to_e { optionalString( host, "pointsTo", host_owner ) };
				if ( !points_to_e.has_value() ) return std::unexpected( std::move( points_to_e.error() ) );

				points_to = std::move( points_to_e.value() );
			}
			else
				return std::unexpected( format_ns::format( "{} hosts must be strings or objects", owner ) );

			if ( hostname.empty() ) return std::unexpected( format_ns::format( "{} hosts must be non-empty", owner ) );

			if ( hostname.find( '/' ) != std::string::npos || hostname.find( ':' ) != std::string::npos )
				return std::unexpected( format_ns::format( "{} host '{}' must be a bare hostname", owner, hostname ) );

			hostname = detail::normalizeHost( std::move( hostname ) );

			const bool wildcard { hostname.starts_with( "*." ) };
			if ( wildcard )
			{
				hostname.erase( 0, 2 );
				include_subdomains = true;
			}

			if ( points_to.has_value() )
			{
				auto& target { points_to.value() };
				const bool bare_target { !target.empty() && target.find_first_of( "/:*" ) == std::string::npos };
				if ( !bare_target )
					return std::unexpected(
						format_ns::format( "{} host '{}' pointsTo must be a bare hostname", owner, hostname ) );

				if ( wildcard )
					return std::unexpected(
						format_ns::format( "{} host '*.{}' pointsTo cannot apply to subdomains", owner, hostname ) );

				target = detail::normalizeHost( std::move( target ) );
				if ( target == hostname )
					return std::unexpected( format_ns::format( "{} host '{}' points to itself", owner, hostname ) );
			}

			if ( pool_subdomains && !settings.group.has_value() ) settings.group = hostname;

			if ( std::ranges::any_of( url_class.hosts, [ & ]( const Host& held ) { return held.host == hostname; } ) )
				return std::unexpected( format_ns::format( "{} lists host '{}' twice", owner, hostname ) );

			url_class.hosts.emplace_back(
				Host { .host = std::move( hostname ),
				       .settings = std::move( settings ),
				       .include_subdomains = include_subdomains,
				       .points_to = std::move( points_to ) } );
		}

		for ( const auto& route_value : routes )
		{
			if ( !route_value.isObject() )
				return std::unexpected( format_ns::format( "{} routes must be objects", owner ) );

			auto export_name { requiredString( route_value, "export", "URL Class route" ) };

			if ( !export_name ) return std::unexpected( std::move( export_name.error() ) );

			auto route_path { optionalString( route_value, "path", "URL Class route" ) };

			if ( !route_path ) return std::unexpected( std::move( route_path.error() ) );

			auto path_prefix { optionalString( route_value, "pathPrefix", "URL Class route" ) };

			if ( !path_prefix ) return std::unexpected( std::move( path_prefix.error() ) );

			if ( route_path->has_value() && path_prefix->has_value() )
				return std::unexpected( "URL Class routes cannot define both 'path' and 'pathPrefix'" );

			auto route_remove_query_e { queryNames( route_value, "removeQuery", owner ) };
			if ( !route_remove_query_e.has_value() ) return std::unexpected( route_remove_query_e.error() );

			Route route { .export_name = std::move( *export_name ),
			              .path = std::move( *route_path ),
				.path_prefix = std::move( *path_prefix ),
				.remove_query = std::move( route_remove_query_e.value() )
			};
			if ( !route_value[ "urlPattern" ].isNull() )
			{
				const auto matcher_e { URLPatternMatcher::create( route_value[ "urlPattern" ] ) };
				if ( !matcher_e.has_value() ) return std::unexpected( matcher_e.error() );

				route.matcher = matcher_e.value();
			}

			if ( const auto& query { route_value[ "query" ] }; !query.isNull() )
			{
				if ( !query.isObject() ) return std::unexpected( "URL Class route 'query' must be an object" );

				for ( const auto& key : query.getMemberNames() )
				{
					if ( !query[ key ].isString() )
						return std::unexpected( "URL Class route query values must be strings" );

					route.query.emplace_back( key, query[ key ].asString() );
				}
			}

			url_class.routes.emplace_back( std::move( route ) );
		}

		const auto index { registry->m_classes.size() };
		for ( const auto& host : url_class.hosts ) registry->m_host_classes[ host.host ].push_back( index );

		registry->m_script_classes[ url_class.script ].push_back( index );
		registry->m_classes.emplace_back( std::move( url_class ) );
	}

	spdlog::info(
		"Downloader routes: {} URL class(es) ({} from parsers) across {} parser module(s)",
		registry->m_classes.size(),
		registry->m_classes.size() - std::min( registry->m_classes.size(), external_classes ),
		registry->m_parsers.size() );
	return registry;
}

std::expected< std::optional< ScriptRoute >, std::string > ScriptRegistry::route(
	const std::string_view url,
	const bool allow_discovery ) const
{
	const auto current { m_current.load() };

	if ( current != nullptr ) return current->route( url, allow_discovery );

	const auto generation { m_learned->generation.load() };
	if ( allow_discovery )
	{
		const std::scoped_lock lock { m_route_cache_mutex };
		const auto cached { m_route_cache.find( url ) };
		if ( cached != m_route_cache.end() && cached->second.generation == generation ) return cached->second.route;
	}

	const auto parsed { ada::parse< ada::url_aggregator >( url ) };

	if ( !parsed ) return std::unexpected( format_ns::format( "Invalid URL: {}", url ) );

	const std::string host { detail::normalizeHost( std::string { parsed->get_hostname() } ) };
	const auto [ candidates, learned_host ] { candidateClasses( host ) };
	ada::url_search_params parameters { parsed->get_search() };
	std::optional< ScriptRoute > match {};
	std::optional< std::string > recognized_class;
	for ( const auto& [ index, configured_domain ] : candidates )
	{
		const auto& url_class { m_classes[ index ] };
		for ( const auto& pattern : url_class.page_patterns )
		{
			const auto match_e { pattern.matcher->match( url, parameters ) };
			if ( !match_e.has_value() ) return std::unexpected( match_e.error() );

			if ( match_e.value().has_value() ) recognized_class = url_class.name;
		}

		for ( const auto& route : url_class.routes )
		{
			auto captured_e { matchRoute( route, url, parsed.value(), parameters ) };
			if ( !captured_e.has_value() ) return std::unexpected( captured_e.error() );

			auto& captured { captured_e.value() };
			if ( !captured.has_value() ) continue;

			ScriptRoute candidate {
				.url_class = url_class.name,
				.script = url_class.script,
				.export_name = route.export_name,
				.configured_domain = configured_domain,
				.parameters = std::move( captured.value() ),
				.bytecode = m_bytecode
			};

			if ( match.has_value() )
				return std::unexpected(
					format_ns::format(
						"URL matches both URL Class '{}' export '{}' and URL Class '{}' export '{}': {}",
						match->url_class,
						match->export_name,
						candidate.url_class,
						candidate.export_name,
						url ) );

			match = std::move( candidate );
			break;
		}
	}

	if ( !match.has_value() && recognized_class.has_value() )
	{
		return std::unexpected(
			format_ns::format(
				"Skipped URL: no parser found to handle URL class '{}': {}", recognized_class.value(), url ) );
	}

	// An unclaimed HTTP URL is inspected as a possible file, regardless of its path or host.
	if ( !match.has_value() && ( parsed->get_protocol() == "http:" || parsed->get_protocol() == "https:" ) )
	{
		const auto& discoverers { m_discoverers };

		if ( allow_discovery && !learned_host && !discoverers.empty() )
		{
			return std::optional< ScriptRoute > { ScriptRoute {
				.url_class = "discovery",
				.script = m_parser_directory / "packages/sdk/discovery.js",
				.export_name = "__discover",
				.configured_domain = std::string { host },
				.discovery_scripts = discoverers,
				.bytecode = m_bytecode } };
		}

		match = ScriptRoute { .url_class = "file", .download = true };
	}

	if ( !allow_discovery ) return match;

	return cacheRoute( url, generation, std::move( match ) );
}

std::optional< ScriptRoute > ScriptRegistry::cacheRoute(
	const std::string_view url,
	const std::uint64_t generation,
	std::optional< ScriptRoute > route ) const
{
	if ( !route.has_value() ) return route;

	const std::scoped_lock lock { m_route_cache_mutex };
	if ( m_route_cache.size() >= 1024 ) m_route_cache.clear();

	m_route_cache.insert_or_assign( std::string { url }, CachedRoute { generation, route.value() } );
	return route;
}

static std::string joinNames( const std::vector< std::string >& names )
{
	std::string joined {};
	for ( const auto& name : names )
	{
		if ( !joined.empty() ) joined += ", ";

		joined += name;
	}

	return joined;
}

// Ephemeral values such as signed tokens are redacted from the logged input.
static void logNormalization( const std::string_view url, const NormalizedUrl& normalized, const UrlRules& rules )
{
	const auto input { detail::redactUrlQuery( url, rules.ephemeral_query ) };
	if ( input == normalized.url )
	{
		return;
	}

	spdlog::debug(
		"URL normalization: {} -> {} (sorted: {}, removed: [{}], ephemeral: [{}])",
		input,
		normalized.url,
		rules.sort_query,
		joinNames( rules.remove_query ),
		joinNames( rules.ephemeral_query ) );
}

ScriptRegistry::ClassCandidates ScriptRegistry::candidateClasses( const std::string_view host ) const
{
	ClassCandidates candidates {};
	std::string_view suffix { host };
	while ( !suffix.empty() )
	{
		const auto found { m_host_classes.find( suffix ) };
		if ( found != m_host_classes.end() )
		{
			// Check the exact host first so the longest configured suffix wins.
			for ( const auto index : found->second ) candidates.classes.try_emplace( index, suffix );
		}

		const auto dot { suffix.find( '.' ) };
		if ( dot == std::string_view::npos ) break;

		suffix.remove_prefix( dot + 1 );
	}

	const auto learned_parser { learnedParser( host ) };
	const auto learned_script { learned_parser.has_value() ? parserScript( learned_parser.value() ) : std::nullopt };
	if ( !learned_script.has_value() ) return candidates;

	const auto found { m_script_classes.find( learned_script.value() ) };
	if ( found == m_script_classes.end() ) return candidates;

	candidates.learned_host = true;
	for ( const auto index : found->second ) candidates.classes.try_emplace( index, host );

	return candidates;
}

std::expected< std::optional< Json::Value >, std::string > ScriptRegistry::matchRoute(
	const Route& route,
	const std::string_view url,
	const ada::url_aggregator& parsed,
	ada::url_search_params& parameters )
{
	if ( route.path.has_value() && *route.path != parsed.get_pathname() ) return std::nullopt;

	if ( route.path_prefix.has_value() && !parsed.get_pathname().starts_with( *route.path_prefix ) )
		return std::nullopt;

	Json::Value captured;
	if ( route.matcher != nullptr )
	{
		auto match_e { route.matcher->match( url, parameters ) };
		if ( !match_e.has_value() ) return std::unexpected( match_e.error() );

		auto& result { match_e.value() };
		if ( !result.has_value() ) return std::nullopt;

		captured = std::move( result.value() );
	}

	for ( const auto& [ name, value ] : route.query )
	{
		if ( !parameters.has( name, value ) ) return std::nullopt;
	}

	return captured;
}

std::expected< UrlRules, std::string > ScriptRegistry::urlRules( const ada::url_aggregator& parsed ) const
{
	const std::string url { parsed.get_href() };
	const std::string host { detail::normalizeHost( std::string { parsed.get_hostname() } ) };
	ada::url_search_params parameters { parsed.get_search() };
	UrlRules rules {};
	bool host_sort { true };
	std::optional< bool > matched_sort {};
	for ( const auto& [ index, configured_domain ] : candidateClasses( host ).classes )
	{
		const auto& url_class { m_classes[ index ] };
		host_sort = host_sort && url_class.sort_query_parameters;
		appendNames( rules.remove_query, url_class.remove_query );
		appendNames( rules.ephemeral_query, url_class.ephemeral_query );

		bool class_matched {};
		for ( const auto& pattern : url_class.page_patterns )
		{
			const auto match_e { pattern.matcher->match( url, parameters ) };
			if ( !match_e.has_value() ) return std::unexpected( match_e.error() );

			if ( !match_e.value().has_value() ) continue;

			class_matched = true;
			appendNames( rules.remove_query, pattern.remove_query );
		}

		for ( const auto& route : url_class.routes )
		{
			const auto captured_e { matchRoute( route, url, parsed, parameters ) };
			if ( !captured_e.has_value() ) return std::unexpected( captured_e.error() );

			if ( !captured_e.value().has_value() ) continue;

			class_matched = true;
			appendNames( rules.remove_query, route.remove_query );
		}

		if ( class_matched && !matched_sort.has_value() ) matched_sort = url_class.sort_query_parameters;
	}

	// A URL no class pattern claims is unsorted if any class on its host opts out.
	rules.sort_query = matched_sort.value_or( host_sort );
	return rules;
}

ada::url_aggregator ScriptRegistry::withPointedHost( ada::url_aggregator url ) const
{
	const std::string host { detail::normalizeHost( std::string { url.get_hostname() } ) };
	const auto found { m_host_classes.find( host ) };
	if ( found == m_host_classes.end() ) return url;

	for ( const auto index : found->second )
	{
		for ( const auto& configured : m_classes[ index ].hosts )
		{
			const bool pointed { configured.host == host && configured.points_to.has_value() };
			if ( !pointed ) continue;

			url.set_hostname( configured.points_to.value() );
			return url;
		}
	}

	return url;
}

std::expected< NormalizedUrl, std::string > ScriptRegistry::normalizeUrl( const std::string_view url ) const
{
	const auto current { m_current.load() };

	if ( current != nullptr ) return current->normalizeUrl( url );

	auto parsed_e { parseUrl( url ) };
	if ( !parsed_e.has_value() )
	{
		spdlog::debug( "URL normalization rejected {}: {}", url, parsed_e.error() );
		return std::unexpected( parsed_e.error() );
	}

	auto parsed { withPointedHost( collapsePathSlashes( std::move( parsed_e.value() ) ) ) };
	const auto rules_e { urlRules( parsed ) };
	if ( !rules_e.has_value() )
	{
		spdlog::debug( "URL normalization rejected {}: {}", url, rules_e.error() );
		return std::unexpected( rules_e.error() );
	}

	const auto& rules { rules_e.value() };
	auto normalized { applyUrlRules( std::move( parsed ), rules ) };
	if ( spdlog::should_log( spdlog::level::debug ) ) logNormalization( url, normalized, rules );

	return normalized;
}

std::expected< Json::Value, std::string > ScriptRegistry::normalizeOptionUrls( Json::Value options ) const
{
	const auto current { m_current.load() };

	if ( current != nullptr ) return current->normalizeOptionUrls( std::move( options ) );

	const auto& declared { std::as_const( options )[ "urls" ] };
	if ( declared.isArray() )
	{
		for ( auto& entry : options[ "urls" ] )
		{
			if ( !entry.isObject() || !entry[ "url" ].isString() ) continue;

			auto normalized_e { normalizeUrl( entry[ "url" ].asString() ) };
			if ( !normalized_e.has_value() ) return std::unexpected( normalized_e.error() );

			entry[ "url" ] = std::move( normalized_e.value().url );
		}
	}

	const auto& discovered { std::as_const( options )[ "discoveredUrls" ] };
	if ( discovered.isArray() )
	{
		for ( auto& url : options[ "discoveredUrls" ] )
		{
			if ( !url.isString() ) continue;

			auto normalized_e { normalizeUrl( url.asString() ) };
			if ( !normalized_e.has_value() ) return std::unexpected( normalized_e.error() );

			url = std::move( normalized_e.value().url );
		}
	}

	return options;
}

std::optional< LaneSettings > ScriptRegistry::configuredLaneSettings( const std::string_view host ) const
{
	const auto current { m_current.load() };

	if ( current != nullptr ) return current->configuredLaneSettings( host );

	const std::string normalized { detail::normalizeHost( std::string { host } ) };

	for ( const auto& url_class : m_classes )
	{
		for ( const Host& held : url_class.hosts )
		{
			if ( held.host == normalized ) return held.settings;
		}
	}

	const Host* family {};

	for ( const auto& url_class : m_classes )
	{
		for ( const Host& held : url_class.hosts )
		{
			if ( !held.include_subdomains || !detail::isSubdomainOf( normalized, held.host ) ) continue;

			//! Longest configured suffix wins, so a nested family beats the one above it.
			if ( family == nullptr || held.host.size() > family->host.size() ) family = &held;
		}
	}

	if ( family == nullptr ) return std::nullopt;

	return family->settings;
}

std::optional< LaneSettings > ScriptRegistry::classSettings( const std::string_view url_class ) const
{
	const auto current { m_current.load() };

	if ( current != nullptr ) return current->classSettings( url_class );

	for ( const auto& held : m_classes )
	{
		if ( held.name == url_class ) return held.settings;
	}

	return std::nullopt;
}

std::optional< std::string > ScriptRegistry::learnedParser( const std::string_view host ) const
{
	const std::scoped_lock lock { m_learned->mutex };
	const auto learned { m_learned->parsers.find( detail::normalizeHost( std::string { host } ) ) };
	if ( learned == m_learned->parsers.end() ) return std::nullopt;

	return learned->second;
}

LaneSettings ScriptRegistry::laneSettings( const std::string_view host ) const
{
	const auto current { m_current.load() };
	if ( current != nullptr ) return current->laneSettings( host );

	const auto generation { m_learned->generation.load() };
	{
		const std::scoped_lock lock { m_route_cache_mutex };
		const auto found { m_lane_cache.find( host ) };
		if ( found != m_lane_cache.end() && found->second.first == generation ) return found->second.second;
	}
	LaneSettings resolved { m_defaults };

	const auto configured { configuredLaneSettings( host ) };

	if ( configured.has_value() )
		resolved = configured.value();
	else
	{
		const auto learned { learnedParser( host ) };
		if ( learned.has_value() )
		{
			const auto script { parserScript( learned.value() ) };
			const auto found { script.has_value() ? m_script_classes.find( script.value() ) : m_script_classes.end() };
			if ( found != m_script_classes.end() && !found->second.empty() )
				resolved = m_classes[ found->second.front() ].settings;
		}
	}
	{
		const std::scoped_lock lock { m_route_cache_mutex };
		if ( m_lane_cache.size() >= 1024 ) m_lane_cache.clear();

		m_lane_cache.insert_or_assign( std::string { host }, std::pair { generation, resolved } );
	}
	return resolved;
}

std::optional< std::filesystem::path > ScriptRegistry::parserScript( const std::string_view id ) const
{
	const auto current { m_current.load() };
	if ( current != nullptr ) return current->parserScript( id );

	const auto found { m_parser_scripts.find( std::string { id } ) };
	if ( found == m_parser_scripts.end() ) return std::nullopt;

	return found->second;
}

std::optional< std::string > ScriptRegistry::classParser( const std::string_view name ) const
{
	const auto current { m_current.load() };
	if ( current != nullptr ) return current->classParser( name );

	for ( const auto& url_class : m_classes )
	{
		if ( url_class.name != name ) continue;

		for ( const auto& [ parser, script ] : m_parser_scripts )
		{
			if ( script == url_class.script ) return parser;
		}
	}
	return std::nullopt;
}

folly::coro::Task< std::expected< void, std::string > > ScriptRegistry::loadParserHosts(
	ParserHostPersistence* persistence )
{
	if ( persistence == nullptr ) co_return std::expected< void, std::string > {};

	const auto hosts_e { co_await persistence->loadAll() };
	if ( !hosts_e.has_value() ) co_return std::unexpected( hosts_e.error() );

	const std::scoped_lock lock { m_learned->mutex };
	m_learned->persistence = persistence;
	for ( const auto& entry : hosts_e.value() )
		m_learned->parsers[ detail::normalizeHost( entry.host ) ] = entry.parser;

	m_learned->generation.fetch_add( 1 );

	co_return std::expected< void, std::string > {};
}

std::expected< void, std::string > ScriptRegistry::setParserHost( std::string host, std::string parser )
{
	if ( !parserScript( parser ).has_value() ) return std::unexpected( "Unknown parser: " + parser );

	const std::scoped_lock lock { m_learned->mutex };
	m_learned->parsers[ detail::normalizeHost( std::move( host ) ) ] = std::move( parser );
	m_learned->generation.fetch_add( 1 );
	return {};
}

void ScriptRegistry::removeParserHost( const std::string_view host )
{
	const std::scoped_lock lock { m_learned->mutex };
	m_learned->parsers.erase( detail::normalizeHost( std::string { host } ) );
	m_learned->generation.fetch_add( 1 );
}

folly::coro::Task< std::expected< void, std::string > > ScriptRegistry::learnHost(
	std::string host,
	std::string url_class )
{
	const std::string normalized { detail::normalizeHost( std::string { host } ) };
	if ( normalized.empty() ) co_return std::unexpected( "A learned host must not be empty" );

	const auto parser { classParser( url_class ) };
	if ( !parser.has_value() ) co_return std::unexpected( "Unknown parser URL class: " + std::string { url_class } );

	ParserHostPersistence* persistence {};
	{
		const std::scoped_lock lock { m_learned->mutex };
		const auto existing { m_learned->parsers.find( normalized ) };
		if ( existing != m_learned->parsers.end() ) co_return std::expected< void, std::string > {};

		persistence = m_learned->persistence;
	}
	std::string parser_id { parser.value() };
	if ( persistence != nullptr )
	{
		const auto saved_e { co_await persistence->discover( normalized, parser_id ) };
		if ( !saved_e.has_value() ) co_return std::unexpected( saved_e.error() );

		parser_id = saved_e.value().parser;
	}
	const auto saved_e { setParserHost( normalized, parser_id ) };
	if ( !saved_e.has_value() ) co_return saved_e;

	spdlog::info( "Downloader learned host {} for parser '{}'", normalized, parser_id );
	co_return std::expected< void, std::string > {};
}

std::expected< ScriptRoute, std::string > ScriptRegistry::generatorRoute( const Json::Value& invocation ) const
{
	const auto current { m_current.load() };
	if ( current != nullptr ) return current->generatorRoute( invocation );

	if ( !invocation[ "parser" ].isString() || !invocation[ "handler" ].isString()
	     || !invocation[ "parameters" ].isObject() )
		return std::unexpected( "Invalid generator invocation" );

	const auto selected { parserScript( invocation[ "parser" ].asString() ) };
	if ( !selected.has_value() )
		return std::unexpected( "Required downloader parser is not available: " + invocation[ "parser" ].asString() );

	for ( const auto& parser : m_parsers )
	{
		const auto& metadata { parser[ "metadata" ] };
		if ( metadata[ "id" ] != invocation[ "parser" ] ) continue;

		if ( ( m_parser_directory / parser[ "script" ].asString() ).lexically_normal() != selected.value() ) continue;

		for ( const auto& handler : metadata[ "handlers" ] )
		{
			if ( handler[ "id" ] != invocation[ "handler" ] ) continue;

			return ScriptRoute {
				.script = selected.value(),
				.export_name = handler[ "id" ].asString(),
				.parameters = invocation[ "parameters" ],
				.bytecode = m_bytecode
			};
		}
		return std::unexpected( "Generator is not available: " + invocation[ "handler" ].asString() );
	}
	return std::unexpected( "Required downloader parser is not available: " + invocation[ "parser" ].asString() );
}

} // namespace idhan::downloader
