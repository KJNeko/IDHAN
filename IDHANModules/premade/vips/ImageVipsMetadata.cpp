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
	if ( !VIPS_MIMES.contains( data.mime_name ) ) return std::unexpected( ModuleError { "Unsupported mime type" } );

	VipsModuleSource source { data.file };
	if ( !source.valid() ) return std::unexpected( ModuleError { "Failed to open the file as a vips source" } );

	// Nothing here needs pixels, only the header -- so the source is read lazily and a large image
	// never lands in this process at all.
	VipsImage* const image_ptr { vips_image_new_from_source( source.get(), "", nullptr ) };

	if ( image_ptr == nullptr )
	{
		return std::unexpected( ModuleError { "Failed to load image" } );
	}

	spdlog::debug( "Decoded image" );

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