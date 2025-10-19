//
// Created by kj16609 on 10/13/25.
//

#include "paths.hpp"

namespace idhan
{
std::vector< std::filesystem::path > getModulePaths()
{
	constexpr std::array< std::string_view, 2 > module_paths { { "./modules", "/usr/share/idhan/modules" } };

	std::vector< std::filesystem::path > paths {};
#ifdef __linux__
#define MODULE_EXT ".so"
#endif

#ifndef MODULE_EXT
#error "No module extension given for this OS. Please report to dev"
#endif

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

	constexpr std::array< std::string_view, 2 > parser_paths { { "./mime", "/usr/share/idhan/mime" } };

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
			if ( std::filesystem::exists( "./static" ) ) static_path = std::filesystem::absolute( "./static" );
			static_path = IDHAN_STATIC_PATH;
		} );

	return static_path;
}
} // namespace idhan