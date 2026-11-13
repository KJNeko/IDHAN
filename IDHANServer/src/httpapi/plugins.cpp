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
#include "idhan/http/routing/ReflectedRoutes.hpp"
#include "logging/log.hpp"
#include "paths.hpp"

namespace idhan::httpapi
{

[[nodiscard]] static std::optional< PluginResponse > parseManifest(
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

	PluginResponse response {
		std::string { id },
		std::string { name },
		std::string { version },
		std::string { host_api },
		{},
		{},
		dir,
		std::format( "/plugins/{}/{}", dir, entry_file )
	};
	std::string_view description {};
	if ( root[ "description" ].get_string().get( description ) == simdjson::SUCCESS )
		response.description.emplace( description );

	simdjson::dom::array panels {};
	if ( root[ "panels" ].get_array().get( panels ) == simdjson::SUCCESS )
		response.panels.emplace( http::JsonDocument { simdjson::minify( panels ) } );

	return response;
}

[[nodiscard]] static std::vector< PluginResponse > scanPlugins()
{
	std::vector< PluginResponse > response;

	const auto root { getPluginsPath() };
	std::error_code ec {};
	if ( std::filesystem::exists( root, ec ) )
	{
		for ( const auto& entry : std::filesystem::directory_iterator( root, ec ) )
		{
			if ( !entry.is_directory() ) continue;
			const auto manifest_path { entry.path() / "manifest.json" };
			if ( !std::filesystem::exists( manifest_path ) ) continue;

			auto parsed { parseManifest( manifest_path, entry.path().filename().string() ) };
			if ( !parsed ) continue;

			response.push_back( std::move( parsed.value() ) );
		}
	}
	else
	{
		log::info( "Plugins path {} does not exist; serving an empty plugin index", root.string() );
	}

	return response;
}

static std::mutex g_index_mutex {};
static std::optional< std::vector< PluginResponse > > g_index {};

http::HTTPTask< http::JsonResult< std::vector< PluginResponse > > > listPlugins( http::Query< ListPluginsQuery > query )
{
	std::vector< PluginResponse > index {};
	{
		std::lock_guard< std::mutex > lock { g_index_mutex };
		if ( query.value.rescan.value_or( false ) || !g_index ) g_index = scanPlugins();
		index = *g_index;
	}

	co_return http::JsonResponse< std::vector< PluginResponse > > { std::move( index ) };
}

} // namespace idhan::httpapi
