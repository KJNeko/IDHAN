//
// Created by kj16609 on 6/12/25.
//
#include "ImageVipsThumbnailer.hpp"

#include <vips/vips.h>

#include <unordered_map>

#include "logging/format_ns.hpp"
#include "vips.hpp"

using namespace idhan;

std::vector< std::string_view > ImageVipsThumbnailer::handleableMimes()
{
	return vipsHandleable();
}

std::expected< ThumbnailInfo, ModuleError > ImageVipsThumbnailer::createThumbnailRaw(
	ModuleCallData& data,
	std::size_t width,
	std::size_t height )
{
	const auto& [ data_view, mime_name, extra ] = data;

	// Only attempt formats we declare handleable (handleableMimes() is derived from this same map).
	if ( const auto it = VIPS_FUNC_MAP.find( mime_name ); it == VIPS_FUNC_MAP.end() || it->second == nullptr )
		return std::unexpected( ModuleError { "Unsupported mime type" } );

	VipsImage* thumb_raw { nullptr };
	if ( vips_thumbnail_buffer(
			 const_cast< void* >( static_cast< const void* >( data_view.data() ) ),
			 data_view.size(),
			 &thumb_raw,
			 static_cast< int >( width ),
			 "height",
			 static_cast< int >( height ),
			 nullptr )
	     != 0 )
		return std::unexpected( ModuleError { "Failed to generate thumbnail" } );
	VipsImagePtr thumb { thumb_raw };

	// Downstream (ThumbnailInfo) expects packed 3-band sRGB uchar data. vips_thumbnail already
	// colour-manages to sRGB in the common case; this is a safety net.
	if ( vips_image_get_interpretation( thumb.get() ) != VIPS_INTERPRETATION_sRGB )
	{
		VipsImage* srgb_raw { nullptr };
		if ( vips_colourspace( thumb.get(), &srgb_raw, VIPS_INTERPRETATION_sRGB, nullptr ) != 0 )
			return std::unexpected( ModuleError { "Failed to convert to sRGB" } );
		thumb.reset( srgb_raw );
	}

	if ( vips_image_get_bands( thumb.get() ) > 3 )
	{
		VipsImage* flat_raw { nullptr };
		if ( vips_flatten( thumb.get(), &flat_raw, nullptr ) != 0 )
			return std::unexpected( ModuleError { "Failed to flatten alpha channel" } );
		thumb.reset( flat_raw );
	}

	return idhan::ThumbnailInfo { std::move( thumb ) };
}