//
// Created by kj16609 on 10/13/25.
//

#include "paths.hpp"

std::filesystem::path getStaticPath()
{
	static std::filesystem::path static_path {};
	static std::once_flag static_path_once {};

	std::call_once(
		static_path_once,
		[ & ]()
		{
			if ( !std::filesystem::exists( "./static" ) )
				static_path = IDHAN_STATIC_PATH;
			else
				static_path = std::filesystem::absolute( "./static" );
		} );

	return static_path;
}