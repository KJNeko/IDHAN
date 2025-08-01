//
// Created by kj16609 on 6/12/25.
//
#pragma once
#include <ranges>

using VipsFunc = int ( * )( void*, size_t, VipsImage**, ... );

inline static std::unordered_map< std::string, VipsFunc > VIPS_FUNC_MAP {
	{ "image/png", vips_pngload_buffer },   { "image/jpeg", vips_jpegload_buffer },
	{ "image/webp", vips_webpload_buffer }, { "image/gif", vips_gifload_buffer },
	{ "image/heif", vips_heifload_buffer }, { "image/svg", vips_svgload_buffer },
	{ "image/webp", vips_webpload_buffer }, { "image/tiff", vips_tiffload_buffer }
};

inline std::vector< std::string_view > vipsHandleable()
{
	std::vector< std::string_view > ret {};

	for ( const auto& mime : VIPS_FUNC_MAP | std::views::keys )
	{
		ret.emplace_back( mime );
	}

	return ret;
}
