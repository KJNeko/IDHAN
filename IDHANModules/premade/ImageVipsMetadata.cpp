//
// Created by kj16609 on 6/11/25.
//

#include "ImageVipsMetadata.hpp"

#include <vips/vips8>

#include <cstring>
#include <unordered_map>

#include "vips.hpp"

using namespace idhan;

std::vector< std::string_view > ImageVipsMetadata::handleableMimes()
{
	return vipsHandleable();
}

std::expected< MetadataInfo, ModuleError > ImageVipsMetadata::parseFile(
	const void* data,
	const std::size_t length,
	const std::string mime_name )
{
	VipsImage* image;
	if ( const auto it = VIPS_FUNC_MAP.find( mime_name ); it != VIPS_FUNC_MAP.end() )
	{
		if ( it->second( const_cast< void* >( data ), length, &image, nullptr ) != 0 )
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
	info.m_metadata = MetadataInfoImage {
		.width = static_cast< int >( image->Xsize ),
		.height = static_cast< int >( image->Ysize ),
		.channels = static_cast< std::uint8_t >( image->Bands )
	};

// Fucking macros
#undef IMAGE

	info.m_simple_type = idhan::SimpleMimeType::IMAGE;

	g_object_unref( image );

	return info;
}