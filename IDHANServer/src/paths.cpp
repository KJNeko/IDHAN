//
// Created by kj16609 on 10/13/25.
//

#include "paths.hpp"

std::filesystem::path getStaticPath()
{
	static std::filesystem::path static_path {};
	static bool static_path_set = false;

	if ( !static_path_set )
	{
		if ( !std::filesystem::exists( "./static" ) )
			static_path = IDHAN_STATIC_PATH;
		else
			static_path = std::filesystem::absolute( "./static" );

		idhan::log::info( "Static path set to {}", static_path.string() );
	}

	idhan::log::info( "Returning path {}", static_path.string() );

	return static_path;
}