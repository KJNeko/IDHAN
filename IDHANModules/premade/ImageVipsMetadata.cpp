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

std::expected< MetadataInfo, ModuleError > ImageVipsMetadata::parseFile( ModuleCallData& data )
{
	VipsImage* image;
	if ( const auto it = VIPS_FUNC_MAP.find( data.mime_name ); it != VIPS_FUNC_MAP.end() )
	{
		if ( it->second(
				 const_cast< void* >( static_cast< const void* >( data.file_view.data() ) ),
				 data.file_view.size(),
				 &image,
				 nullptr )
		     != 0 )
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

	info.m_simple_type = idhan::SimpleMimeType::IMAGE_TYPE;

	g_object_unref( image );

	return info;
}