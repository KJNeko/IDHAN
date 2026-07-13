//
// Created by kj16609 on 6/11/25.
//

#include "ImageVipsMetadata.hpp"

#include <vips/vips8>

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
	VipsImage* image_ptr;
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

	const vips::VImage image { image_ptr };

	MetadataInfo info {};
	info.m_metadata = MetadataInfoImage {
		.width = static_cast< int >( image.width() ),
		.height = static_cast< int >( image.height() ),
		.channels = static_cast< std::uint8_t >( image.bands() )
	};

	info.m_simple_type = idhan::SimpleMimeType::IMAGE_TYPE;

	return info;
}