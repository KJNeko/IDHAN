//
// Created by kj16609 on 10/13/25.
//

#include "paths.hpp"

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

std::vector< std::filesystem::path > getModulePaths()
{
	constexpr std::array< std::string_view, 2 > module_paths { { "./modules", IDHAN_MODULES_PATH } };

	std::vector< std::filesystem::path > paths {};

	for ( const auto& search_path : module_paths )
	{
		if ( !std::filesystem::exists( search_path ) ) continue;
		log::info( "Searching for modules at {}", search_path );
		for ( const auto& file : std::filesystem::recursive_directory_iterator( search_path ) )
		{
			if ( !file.is_regular_file() ) continue;
			if ( file.path().extension() == MODULE_EXT ) paths.emplace_back( file.path() );
		}
	}

	return paths;
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

std::vector< std::size_t > getCacheableThumbnailSizes()
{
	// Read live (no call_once): an operator can edit cacheable_sizes while the server runs and have it
	// take effect on the next thumbnail generation. The config layer re-reads the file per call; this is
	// only reached on a cache miss, so the parse cost is dwarfed by the generation it gates.
	return idhan::config::getArray< std::size_t >(
		"thumbnails", "cacheable_sizes", std::vector< std::size_t > { 128, 256, 512 } );
}

} // namespace idhan
