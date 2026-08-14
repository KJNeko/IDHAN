#include "paths.hpp"

#include <set>

#include "Config.hpp"

namespace idhan
{

#ifdef __linux__
#define MODULE_EXT ".so"
#elif defined( _WIN32 )
#define MODULE_EXT ".dll"
#else
#error "No module extension defined for this OS"
#endif

std::filesystem::path getExecutableDir()
{
	static std::filesystem::path executable_dir {};
	static std::once_flag executable_dir_once {};

	std::call_once(
		executable_dir_once,
		[]()
		{
#ifdef __linux__
			std::error_code error {};
			const auto self { std::filesystem::read_symlink( "/proc/self/exe", error ) };

			if ( !error )
			{
				executable_dir = self.parent_path();
				return;
			}

			log::warn( "Could not resolve /proc/self/exe; falling back to the working directory" );
#endif
			executable_dir = std::filesystem::current_path();
		} );

	return executable_dir;
}

std::vector< std::filesystem::path > getModulePaths()
{
	const std::array< std::filesystem::path, 3 > module_paths {
		{ getExecutableDir() / "modules", "./modules", IDHAN_MODULES_PATH }
	};

	std::vector< std::filesystem::path > paths {};

	std::set< std::filesystem::path > seen {};

	for ( const auto& search_path : module_paths )
	{
		if ( !std::filesystem::exists( search_path ) ) continue;

		std::error_code error {};
		const auto canonical_root { std::filesystem::canonical( search_path, error ) };
		if ( error || !seen.insert( canonical_root ).second ) continue;

		log::info( "Searching for modules at {}", canonical_root.string() );

		for ( const auto& file : std::filesystem::recursive_directory_iterator( canonical_root ) )
		{
			if ( !file.is_regular_file() ) continue;
			if ( file.path().extension() == MODULE_EXT ) paths.emplace_back( file.path() );
		}
	}

	if ( paths.empty() )
		log::error(
			"No module libraries found. Searched {}/modules, ./modules and {} -- without them the server "
			"has no metadata parsers or thumbnailers.",
			getExecutableDir().string(),
			IDHAN_MODULES_PATH );

	return paths;
}

std::filesystem::path getModuleRunnerPath()
{
	static std::filesystem::path runner_path {};
	static std::once_flag runner_path_once {};

	std::call_once(
		runner_path_once,
		[]()
		{
			if ( const auto configured {
					 idhan::config::getSilentDefault< std::string >( "modules", "runner_path", "" ) };
		         !configured.empty() )
			{
				runner_path = configured;
				return;
			}

			const std::array< std::filesystem::path, 3 > candidates {
				{ getExecutableDir() / "IDHANModuleRunner", "./IDHANModuleRunner", IDHAN_MODULE_RUNNER_PATH }
			};

			for ( const auto& candidate : candidates )
			{
				if ( std::filesystem::exists( candidate ) )
				{
					runner_path = std::filesystem::absolute( candidate );
					log::info( "Module runner: {}", runner_path.string() );
					return;
				}
			}

			log::error(
				"Could not find IDHANModuleRunner (looked in {}, ./ and {}). Modules run out of process, so "
				"without it every module library is skipped and no parsers or thumbnailers will exist. Set "
				"[modules] runner_path to override.",
				getExecutableDir().string(),
				IDHAN_MODULE_RUNNER_PATH );

			runner_path = IDHAN_MODULE_RUNNER_PATH;
		} );

	return runner_path;
}

std::vector< std::filesystem::path > getMimeParserPaths()
{
	std::vector< std::filesystem::path > paths {};

	constexpr std::array< std::string_view, 2 > parser_paths { { "./mime", IDHAN_MIME_PATH } };

	for ( const auto& search_path : parser_paths )
	{
		if ( !std::filesystem::exists( search_path ) ) continue;
		log::info( "Searching for mime parsers at {}", search_path );
		for ( const auto& file : std::filesystem::recursive_directory_iterator( search_path ) )
		{
			if ( !file.is_regular_file() ) continue;
			if ( file.path().extension() == ".idhanmime" ) paths.emplace_back( file.path() );
		}
	}

	log::debug( "Found {} mime parsers", paths.size() );

	return paths;
}

std::filesystem::path getStaticPath()
{
	static std::filesystem::path static_path {};
	static std::once_flag static_path_once {};

	std::call_once(
		static_path_once,
		[ & ]()
		{
			if ( std::filesystem::exists( "./static" ) )
				static_path = std::filesystem::absolute( "./static" );
			else
				static_path = IDHAN_STATIC_PATH;
		} );

	return static_path;
}

std::filesystem::path getThumbnailsPath()
{
	static std::filesystem::path thumbnails_path {};
	static std::once_flag thumbnails_path_once {};

	std::call_once(
		thumbnails_path_once,
		[]() { thumbnails_path = idhan::config::get< std::string >( "thumbnails", "path", "./thumbnails" ); } );

	return thumbnails_path;
}

std::filesystem::path getPluginsPath()
{
	static std::filesystem::path plugins_path {};
	static std::once_flag plugins_path_once {};

	std::call_once(
		plugins_path_once,
		[]()
		{
			// Empty (unconfigured) → default under the static root so the static router serves the bundles.
			const auto configured { idhan::config::getSilentDefault< std::string >( "plugins", "path", "" ) };
			if ( !configured.empty() )
				plugins_path = configured;
			else
				plugins_path = getStaticPath() / "plugins";
		} );

	return plugins_path;
}

std::vector< std::size_t > getCacheableThumbnailSizes()
{
	return idhan::config::getArray< std::size_t >(
		"thumbnails", "cacheable_sizes", std::vector< std::size_t > { 128, 256, 512 } );
}

bool getThumbnailCachingEnabled()
{
	return idhan::config::getSilentDefault< bool >( "thumbnails", "cache", true );
}

bool getPurgeThumbnailsOnBoot()
{
	// Silent for the same reason -- an optional debugging toggle should not nag on every boot.
	return idhan::config::getSilentDefault< bool >( "thumbnails", "purge_on_boot", false );
}

} // namespace idhan
