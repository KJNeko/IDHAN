#include "plugins.hpp"

#include <folly/io/IOBuf.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <simdjson.h>
#include <string>
#include <string_view>

#include "apiAuth.hpp"
#include "idhan/db/DBContext.hpp"
#include "logging/log.hpp"
#include "paths.hpp"

namespace idhan::httpapi
{

[[nodiscard]] static std::optional< std::string > parseManifest(
	const std::filesystem::path& path,
	const std::string& dir )
{
	std::ifstream file { path, std::ios::binary };
	if ( !file )
	{
		log::warn( "Skipping plugin '{}': could not open {}", dir, path.string() );
		return std::nullopt;
	}
	const std::string content { std::istreambuf_iterator< char > { file }, std::istreambuf_iterator< char > {} };

	simdjson::dom::parser parser;
	simdjson::dom::element root;
	if ( const auto error { parser.parse( content ).get( root ) } )
	{
		log::warn( "Skipping plugin '{}': manifest.json is not valid JSON: {}", dir, simdjson::error_message( error ) );
		return std::nullopt;
	}
	if ( !root.is_object() )
	{
		log::warn( "Skipping plugin '{}': manifest.json is not a JSON object", dir );
		return std::nullopt;
	}

	std::string_view id {}, name {}, version {}, host_api {}, entry_file {};
	if ( root[ "id" ].get_string().get( id ) != simdjson::SUCCESS
	     || root[ "name" ].get_string().get( name ) != simdjson::SUCCESS
	     || root[ "version" ].get_string().get( version ) != simdjson::SUCCESS
	     || root[ "hostApi" ].get_string().get( host_api ) != simdjson::SUCCESS
	     || root[ "entry" ].get_string().get( entry_file ) != simdjson::SUCCESS )
	{
		log::warn( "Skipping plugin '{}': manifest missing a required string field", dir );
		return std::nullopt;
	}

	if ( entry_file.empty() || entry_file.front() == '/' || entry_file.find( ".." ) != std::string_view::npos )
	{
		log::warn( "Skipping plugin '{}': 'entry' must be a relative path within the plugin dir", dir );
		return std::nullopt;
	}

	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.append_key_value( "id", id );
	builder.append_comma();
	builder.append_key_value( "name", name );
	builder.append_comma();
	builder.append_key_value( "version", version );
	builder.append_comma();
	builder.append_key_value( "hostApi", host_api );

	std::string_view description {};
	if ( root[ "description" ].get_string().get( description ) == simdjson::SUCCESS )
	{
		builder.append_comma();
		builder.append_key_value( "description", description );
	}

	simdjson::dom::array panels {};
	if ( root[ "panels" ].get_array().get( panels ) == simdjson::SUCCESS )
	{
		builder.append_comma();
		builder.escape_and_append_with_quotes( "panels" );
		builder.append_colon();
		builder.append_raw( simdjson::minify( panels ) );
	}

	builder.append_comma();
	builder.append_key_value( "dir", dir );
	builder.append_comma();
	builder.append_key_value( "entry", std::format( "/plugins/{}/{}", dir, entry_file ) );
	builder.end_object();

	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	return std::string { *json };
}

[[nodiscard]] static std::string scanPlugins()
{
	simdjson::builder::string_builder builder;
	builder.start_array();

	const auto root { getPluginsPath() };
	std::error_code ec {};
	bool first { true };
	if ( std::filesystem::exists( root, ec ) )
	{
		for ( const auto& entry : std::filesystem::directory_iterator( root, ec ) )
		{
			if ( !entry.is_directory() ) continue;
			const auto manifest_path { entry.path() / "manifest.json" };
			if ( !std::filesystem::exists( manifest_path ) ) continue;

			auto parsed { parseManifest( manifest_path, entry.path().filename().string() ) };
			if ( !parsed ) continue;

			if ( !first ) builder.append_comma();
			first = false;
			builder.append_raw( *parsed );
		}
	}
	else
	{
		log::info( "Plugins path {} does not exist; serving an empty plugin index", root.string() );
	}

	builder.end_array();

	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	return std::string { *json };
}

static std::mutex g_index_mutex {};
static std::optional< std::string > g_index {};

static http::HTTPTask< http::Response > listPlugins( http::RequestContext context )
{
	const auto rescan_e { context.parameters().optional< bool >( "rescan" ) };
	if ( !rescan_e ) co_return http::Response::error( 400, rescan_e.error().message );

	std::string index {};
	{
		std::lock_guard< std::mutex > lock { g_index_mutex };
		if ( rescan_e->value_or( false ) || !g_index ) g_index = scanPlugins();
		index = *g_index;
	}

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( index ) );
}

http::RouteDefinition pluginsRoute( db::DBContext& db )
{
	return http::RouteDefinition {
		http::RouteMetadata { proxygen::HTTPMethod::GET, "/plugins", {}, { apiAuthValidation( db ) } }, listPlugins
	};
}

} // namespace idhan::httpapi
