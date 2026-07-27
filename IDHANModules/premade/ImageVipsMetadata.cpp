//
// Created by kj16609 on 6/11/25.
//

#include "ImageVipsMetadata.hpp"

#include <vips/vips.h>

#include <cstring>
#include <unordered_map>

#include "spdlog/spdlog.h"
#include "vips.hpp"

using namespace idhan;

std::vector< std::string_view > ImageVipsMetadata::handleableMimes()
{
	return vipsHandleable();
}

std::expected< MetadataInfo, ModuleError > ImageVipsMetadata::parseFile( ModuleCallData& data )
{
	VipsImage* image_ptr { nullptr };
	if ( const auto it = VIPS_FUNC_MAP.find( data.mime_name ); it != VIPS_FUNC_MAP.end() && it->second != nullptr )
	{
		if ( it->second(
				 const_cast< void* >( static_cast< const void* >( data.file_view.data() ) ),
				 data.file_view.size(),
				 &image_ptr,
				 nullptr )
		     != 0 )
		{
			return std::unexpected( ModuleError { "Failed to load image" } );
		}

		spdlog::debug( "Decoded image" );
	}
	else
	{
		return std::unexpected( ModuleError { "Unsupported mime type" } );
	}

	if ( !image_ptr )
	{
		return std::unexpected( ModuleError { "Failed to load image" } );
	}

	VipsImagePtr image { image_ptr };

	MetadataInfo info {};
	info.m_metadata = MetadataInfoImage {
		.width = vips_image_get_width( image.get() ),
		.height = vips_image_get_height( image.get() ),
		.channels = static_cast< std::uint8_t >( vips_image_get_bands( image.get() ) )
	};

	info.m_simple_type = idhan::SimpleMimeType::IMAGE_TYPE;

	return info;
}