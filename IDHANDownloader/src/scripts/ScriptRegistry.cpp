#include "scripts/ScriptRegistry.hpp"

#include <json/json.h>
#include <spdlog/spdlog.h>

#include <ada.h>
#include <algorithm>
#include <fstream>
#include <map>
#include <unordered_set>

#include "URLUtils.hpp"
#include "IDHANDownloader/UrlComparison.hpp"
#include "logging/format_ns.hpp"

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
		.rate = RequestRate { .requests = requests.asUInt64(), .seconds = seconds.asUInt64() },
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

//! Reads the optional "lane" object, leaving anything it does not mention at the inherited value.
static std::expected< void, std::string > laneOverrides(
	const Json::Value& object,
	const std::string_view owner,
	LaneSettings& settings )
{
	const auto& value { object[ "lane" ] };

	if ( value.isNull() ) return {};

	if ( !value.isObject() ) return std::unexpected( format_ns::format( "{} lane must be an object", owner ) );

	if ( const auto& bandwidth { value[ "bandwidth" ] }; !bandwidth.isNull() )
	{
		if ( !bandwidth.isUInt64() )
			return std::unexpected( format_ns::format( "{} lane bandwidth must be a byte count", owner ) );

		settings.bytes_per_second = bandwidth.asUInt64();
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

ScriptRegistry::ScriptRegistry( std::filesystem::path parser_directory, LaneSettings defaults ) :
  m_parser_directory( std::move( parser_directory ) ),
  m_bytecode( m_parser_directory ),
  m_defaults( std::move( defaults ) )
{}

std::expected< std::unique_ptr< ScriptRegistry >, std::string > ScriptRegistry::create(
	const std::filesystem::path& url_classes,
	std::filesystem::path parser_directory,
	LaneSettings defaults )
{
	Json::CharReaderBuilder reader {};
	std::vector< std::filesystem::path > files {};
	std::error_code filesystem_error {};
	if ( std::filesystem::is_directory( url_classes, filesystem_error ) )
	{
		for ( const auto& entry : std::filesystem::directory_iterator( url_classes, filesystem_error ) )
			if ( entry.is_regular_file() && entry.path().extension() == ".json" ) files.emplace_back( entry.path() );
		const auto private_directory { url_classes / "private" };
		if ( std::filesystem::is_directory( private_directory, filesystem_error ) )
			for ( const auto& entry : std::filesystem::directory_iterator( private_directory, filesystem_error ) )
				if ( entry.is_regular_file() && entry.path().extension() == ".json" )
					files.emplace_back( entry.path() );
		if ( filesystem_error )
			return std::unexpected(
				format_ns::format(
					"Unable to list URL Classes directory {}: {}", url_classes.string(), filesystem_error.message() ) );
		std::ranges::sort( files );
	}
	else
		files.emplace_back( url_classes );

	if ( files.empty() )
		return std::unexpected( format_ns::format( "No URL Classes JSON files found in {}", url_classes.string() ) );

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
		const auto& classes { root[ "urlClasses" ] };
		if ( !root.isObject() || !classes.isArray() )
			return std::unexpected(
				format_ns::format( "URL Classes file must contain a 'urlClasses' array: {}", file.string() ) );
		for ( const auto& definition : classes )
		{
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
	const Json::Value& classes { merged[ "urlClasses" ] };

	std::unique_ptr< ScriptRegistry > registry {
		new ScriptRegistry( std::move( parser_directory ), std::move( defaults ) )
	};
	std::unordered_set< std::string > names {};
	const auto base_path { std::filesystem::absolute( url_classes.parent_path() ) };

	for ( const auto& value : classes )
	{
		if ( !value.isObject() ) return std::unexpected( "URL Class entries must be objects" );

		auto name { requiredString( value, "name", "URL Class" ) };

		if ( !name ) return std::unexpected( std::move( name.error() ) );
		if ( !names.emplace( *name ).second )
			return std::unexpected( format_ns::format( "Duplicate URL Class name: {}", *name ) );

		const std::string owner { format_ns::format( "URL Class '{}'", *name ) };
		auto script { requiredString( value, "script", owner ) };

		if ( !script ) return std::unexpected( std::move( script.error() ) );

		const auto& hosts { value[ "hosts" ] };
		const auto& routes { value[ "routes" ] };

		if ( !hosts.isArray() ) return std::unexpected( format_ns::format( "{} requires a 'hosts' array", owner ) );

		if ( !routes.isArray() || routes.empty() )
			return std::unexpected( format_ns::format( "{} requires a non-empty 'routes' array", owner ) );

		URLClass url_class { .name = *name, .script = ( base_path / *script ).lexically_normal() };

		if ( value.isMember( "sortQueryParameters" ) )
		{
			const auto& sort { value[ "sortQueryParameters" ] };
			if ( !sort.isBool() )
				return std::unexpected( format_ns::format( "{} sortQueryParameters must be a boolean", owner ) );

			url_class.sort_query_parameters = sort.asBool();
		}

		const auto& ephemeral_query { value[ "ephemeralQuery" ] };
		if ( value.isMember( "ephemeralQuery" ) )
		{
			if ( !ephemeral_query.isArray() )
				return std::unexpected( format_ns::format( "{} ephemeralQuery must be an array", owner ) );

			for ( const auto& parameter : ephemeral_query )
			{
				if ( !parameter.isString() || parameter.asString().empty() )
					return std::unexpected(
						format_ns::format( "{} ephemeralQuery requires non-empty strings", owner ) );

				url_class.ephemeral_query.emplace_back( parameter.asString() );
			}
		}

		LaneSettings class_settings { registry->m_defaults };
		auto class_rate { requestRate( value, owner ) };

		if ( !class_rate ) return std::unexpected( std::move( class_rate.error() ) );
		if ( class_rate->defined ) class_settings.rate = class_rate->rate;

		if ( const auto applied { laneOverrides( value, owner, class_settings ) }; !applied )
			return std::unexpected( applied.error() );

		for ( const auto& host : hosts )
		{
			std::string hostname {};
			LaneSettings settings { class_settings };
			bool include_subdomains { class_rate->share_subdomains };
			bool pool_subdomains { class_rate->pool_subdomains };

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
			}
			else
				return std::unexpected( format_ns::format( "{} hosts must be strings or objects", owner ) );

			if ( hostname.empty() ) return std::unexpected( format_ns::format( "{} hosts must be non-empty", owner ) );

			if ( hostname.find( '/' ) != std::string::npos || hostname.find( ':' ) != std::string::npos )
				return std::unexpected( format_ns::format( "{} host '{}' must be a bare hostname", owner, hostname ) );

			hostname = detail::normalizeHost( std::move( hostname ) );

			if ( pool_subdomains && !settings.group.has_value() ) settings.group = hostname;

			if ( std::ranges::any_of( url_class.hosts, [ & ]( const Host& held ) { return held.host == hostname; } ) )
				return std::unexpected( format_ns::format( "{} lists host '{}' twice", owner, hostname ) );

			url_class.hosts.emplace_back(
				Host { .host = std::move( hostname ),
				       .settings = std::move( settings ),
				       .include_subdomains = include_subdomains } );
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

			Route route { .export_name = std::move( *export_name ),
			              .path = std::move( *route_path ),
			              .path_prefix = std::move( *path_prefix ) };

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

		registry->m_classes.emplace_back( std::move( url_class ) );
	}

	spdlog::info( "downloader route: loaded {} URL classes from {}", registry->m_classes.size(), url_classes.string() );
	return registry;
}

std::expected< std::optional< ScriptRoute >, std::string > ScriptRegistry::route( const std::string_view url ) const
{
	const auto parsed { ada::parse< ada::url_aggregator >( url ) };

	if ( !parsed ) return std::unexpected( format_ns::format( "Invalid URL: {}", url ) );

	const std::string host { detail::normalizeHost( std::string { parsed->get_hostname() } ) };
	ada::url_search_params parameters { parsed->get_search() };
	std::optional< ScriptRoute > match {};

	for ( const auto& url_class : m_classes )
	{
		const bool hosted {
			std::ranges::any_of( url_class.hosts, [ & ]( const Host& held ) { return held.host == host; } )
		};

		if ( !hosted ) continue;

		for ( const auto& route : url_class.routes )
		{
			if ( route.path.has_value() && *route.path != parsed->get_pathname() ) continue;
			if ( route.path_prefix.has_value() && !parsed->get_pathname().starts_with( *route.path_prefix ) ) continue;

			const bool query_matches { std::ranges::all_of(
				route.query,
				[ &parameters ]( const auto& item ) { return parameters.has( item.first, item.second ); } ) };

			if ( !query_matches ) continue;

			ScriptRoute candidate {
				.url_class = url_class.name, .script = url_class.script, .export_name = route.export_name
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
		}
	}

	// An unclaimed HTTP URL is inspected as a possible file, regardless of its path or host.
	if ( !match.has_value() && ( parsed->get_protocol() == "http:" || parsed->get_protocol() == "https:" ) )
		match = ScriptRoute { .url_class = "file", .download = true };

	return match;
}

std::vector< std::string > ScriptRegistry::ephemeralQuery( const std::string_view url ) const
{
	const auto parsed { ada::parse< ada::url_aggregator >( url ) };
	if ( !parsed ) return {};

	const auto host { parsed->get_hostname() };
	std::vector< std::string > names {};
	for ( const auto& url_class : m_classes )
	{
		for ( const auto& held : url_class.hosts )
		{
			if ( held.host != host ) continue;

			names.insert( names.end(), url_class.ephemeral_query.begin(), url_class.ephemeral_query.end() );
			break;
		}
	}
	std::ranges::sort( names );
	names.erase( std::unique( names.begin(), names.end() ), names.end() );
	return names;
}

std::string ScriptRegistry::storageUrl( const std::string_view url ) const
{
	const auto parsed { ada::parse< ada::url_aggregator >( url ) };
	if ( !parsed ) return std::string { url };

	const auto matched { route( url ) };
	if ( matched && matched->has_value() && !( **matched ).download )
	{
		for ( const auto& url_class : m_classes )
			if ( url_class.name == ( **matched ).url_class )
				return url_class.sort_query_parameters ? sortUrlQueryParameters( url ) : std::string { url };
	}

	// Automatic file inspection has no parser class. A configured host opt-out wins.
	for ( const auto& url_class : m_classes )
	{
		if ( url_class.sort_query_parameters ) continue;

		for ( const auto& host : url_class.hosts )
			if ( host.host == parsed->get_hostname() ) return std::string { url };
	}
	return sortUrlQueryParameters( url );
}

Json::Value ScriptRegistry::storageOptions( Json::Value options ) const
{
	const auto& declared { std::as_const( options )[ "urls" ] };
	if ( declared.isArray() )
		for ( auto& entry : options[ "urls" ] )
			if ( entry.isObject() && entry[ "url" ].isString() )
				entry[ "url" ] = storageUrl( entry[ "url" ].asString() );

	const auto& discovered { std::as_const( options )[ "discoveredUrls" ] };
	if ( discovered.isArray() )
		for ( auto& url : options[ "discoveredUrls" ] )
			if ( url.isString() ) url = storageUrl( url.asString() );

	return options;
}

std::string ScriptRegistry::comparisonKey( const std::string_view url ) const
{
	return urlComparisonKey( url, ephemeralQuery( url ) );
}

LaneSettings ScriptRegistry::laneSettings( const std::string_view host ) const
{
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

	return family == nullptr ? m_defaults : family->settings;
}

} // namespace idhan::downloader
