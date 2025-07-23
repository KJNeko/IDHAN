//
// Created by kj16609 on 6/11/25.
//

#include "ImageVipsMetadata.hpp"

#include <vips/vips8>
// Fucking macros
#undef IMAGE

#include <cstring>
#include <unordered_map>

#include "vips.hpp"

std::vector< std::string_view > ImageVipsMetadata::handleableMimes()
{
	return vipsHandleable();
}

std::expected< MetadataInfo, ModuleError > ImageVipsMetadata::
	parseImage( void* data, std::size_t length, const std::string mime_name )
{
	using VipsFunc = int ( * )( void*, size_t, VipsImage**, ... );
	// VipsFunc load = vips_pngload_buffer;
	std::unordered_map< std::string, VipsFunc > func_map { { "image/png", vips_pngload_buffer },
		                                                   { "image/jpeg", vips_jpegload_buffer },
		                                                   { "image/webp", vips_webpload_buffer },
		                                                   { "image/gif", vips_gifload_buffer } };

	VipsImage* image;
	if ( const auto it = func_map.find( mime_name ); it != func_map.end() )
	{
		if ( it->second( data, length, &image, nullptr ) != 0 )
		{
			return std::unexpected( ModuleError { "Failed to load image" } );
		}
	}
	else
	{
		return std::unexpected( ModuleError { "Unsupported mime type" } );
	}

	if ( !image )
	{
		return std::unexpected( ModuleError { "Failed to load image" } );
	}

	MetadataInfo info {};
	info.m_metadata = MetadataInfoImage { .width = static_cast< int >( image->Xsize ),
		                                  .height = static_cast< int >( image->Ysize ),
		                                  .channels = static_cast< std::uint8_t >( image->Bands ) };

// Fucking macros
#undef IMAGE

	info.m_simple_type = idhan::SimpleMimeType::IMAGE;

	g_object_unref( image );

	return info;
}