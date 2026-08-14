
#include "PluginAPI.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>

#include "logging/log.hpp"
#include "paths.hpp"

namespace idhan::api
{

//! Cached scan result. Populated lazily on first request and refreshed on ?rescan=true. Guarded by the
//! mutex; scanning is synchronous filesystem work, so the lock is never held across a co_await.
std::optional< Json::Value > g_index {};
std::mutex g_index_mutex {};

//! Parse and validate one plugin's manifest.json. Returns the published index entry (manifest fields
//! plus a resolved bundle URL) or nullopt if the manifest is missing/invalid — a bad plugin is skipped
//! with a warning rather than failing the whole scan.
std::optional< Json::Value > parseManifest( const std::filesystem::path& path, const std::string& dir )
{
	std::ifstream file { path, std::ios::binary };
	if ( !file )
	{
		log::warn( "Skipping plugin '{}': could not open {}", dir, path.string() );
		return std::nullopt;
	}
	const std::string content { std::istreambuf_iterator< char > { file }, std::istreambuf_iterator< char > {} };

	Json::Value manifest {};
	Json::CharReaderBuilder builder {};
	std::string errs {};
	const std::unique_ptr< Json::CharReader > reader { builder.newCharReader() };
	if ( !reader->parse( content.data(), content.data() + content.size(), &manifest, &errs ) )
	{
		log::warn( "Skipping plugin '{}': manifest.json is not valid JSON: {}", dir, errs );
		return std::nullopt;
	}
	if ( !manifest.isObject() )
	{
		log::warn( "Skipping plugin '{}': manifest.json is not a JSON object", dir );
		return std::nullopt;
	}

	constexpr std::array< std::string_view, 5 > required { { "id", "name", "version", "hostApi", "entry" } };
	for ( const auto field : required )
	{
		const std::string key { field };
		if ( !manifest.isMember( key ) || !manifest[ key ].isString() )
		{
			log::warn( "Skipping plugin '{}': manifest missing string field '{}'", dir, field );
			return std::nullopt;
		}
	}

	const auto entry_file { manifest[ "entry" ].asString() };
	if ( entry_file.empty() || entry_file.front() == '/' || entry_file.find( ".." ) != std::string::npos )
	{
		log::warn( "Skipping plugin '{}': 'entry' must be a relative path within the plugin dir", dir );
		return std::nullopt;
	}

	Json::Value out {};
	out[ "id" ] = manifest[ "id" ];
	out[ "name" ] = manifest[ "name" ];
	out[ "version" ] = manifest[ "version" ];
	out[ "hostApi" ] = manifest[ "hostApi" ];
	if ( manifest.isMember( "description" ) && manifest[ "description" ].isString() )
		out[ "description" ] = manifest[ "description" ];
	if ( manifest.isMember( "panels" ) && manifest[ "panels" ].isArray() ) out[ "panels" ] = manifest[ "panels" ];
	out[ "dir" ] = dir;
	// Resolved bundle URL under the static-served /plugins root. The client dynamic-imports this.
	out[ "entry" ] = std::format( "/plugins/{}/{}", dir, entry_file );
	return out;
}

//! Scan the plugins directory for `<dir>/manifest.json` entries and build the index array.
Json::Value scanPlugins()
{
	Json::Value out { Json::arrayValue };

	const auto root { getPluginsPath() };
	std::error_code ec {};
	if ( !std::filesystem::exists( root, ec ) )
	{
		log::info( "Plugins path {} does not exist; serving an empty plugin index", root.string() );
		return out;
	}

	for ( const auto& entry : std::filesystem::directory_iterator( root, ec ) )
	{
		if ( !entry.is_directory() ) continue;
		const auto manifest_path { entry.path() / "manifest.json" };
		if ( !std::filesystem::exists( manifest_path ) ) continue;
		if ( auto parsed { parseManifest( manifest_path, entry.path().filename().string() ) } )
			out.append( std::move( *parsed ) );
	}

	log::info( "Scanned {} for plugins; {} valid", root.string(), out.size() );
	return out;
}

drogon::Task< drogon::HttpResponsePtr > PluginAPI::listPlugins( drogon::HttpRequestPtr req )
{
	const bool rescan { req->getOptionalParameter< bool >( "rescan" ).value_or( false ) };

	// No co_await inside this critical section, so holding the mutex synchronously is safe.
	std::lock_guard< std::mutex > lock { g_index_mutex };
	if ( rescan || !g_index ) g_index = scanPlugins();

	co_return drogon::HttpResponse::newHttpJsonResponse( *g_index );
}

} // namespace idhan::api
