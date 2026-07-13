//
// Created by kj16609 on 6/12/25.
//
#include "ImageVipsThumbnailer.hpp"

#include <vips/vips8>

#include <unordered_map>

#include "logging/format_ns.hpp"
#include "vips.hpp"

using namespace idhan;

std::vector< std::string_view > ImageVipsThumbnailer::handleableMimes()
{
	return vipsHandleable();
}

std::expected< ThumbnailInfo, ModuleError > ImageVipsThumbnailer::createThumbnail(
	ModuleCallData& data,
	std::size_t width,
	std::size_t height )
{
	const auto& [ data_view, mime_name, extra ] = data;

	VipsImage* image_ptr { nullptr };
	if ( const auto it = VIPS_FUNC_MAP.find( mime_name ); it != VIPS_FUNC_MAP.end() && it->second != nullptr )
	{
		if ( it->second(
				 const_cast< void* >( static_cast< const void* >( data_view.data() ) ),
				 data_view.size(),
				 &image_ptr,
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

	if ( !image_ptr )
	{
		return std::unexpected( ModuleError { "Failed to load image" } );
	}

	vips::VImage image { image_ptr };

	const auto source_width { image.width() };
	const auto source_height { image.height() };

	const float source_aspect { static_cast< float >( source_width ) / static_cast< float >( source_height ) };
	const float target_aspect { static_cast< float >( width ) / static_cast< float >( height ) };

	if ( target_aspect > source_aspect )
		width = static_cast< std::size_t >( static_cast< float >( height ) * source_aspect );
	else
		height = static_cast< std::size_t >( static_cast< float >( width ) / source_aspect );

	auto resized { image.resize( static_cast< double >( width ) / static_cast< double >( image.width() ) ) };
	if ( resized.interpretation() != VIPS_INTERPRETATION_sRGB )
	{
		resized = resized.colourspace( VIPS_INTERPRETATION_sRGB );
	}

	if ( resized.bands() > 3 )
	{
		// resized = resized.extract_band( 0, vips::VImage::option()->set( "n", 3 ) );
		resized = resized.flatten();
	}

	idhan::ThumbnailInfo ret { resized };

	return ret;
}