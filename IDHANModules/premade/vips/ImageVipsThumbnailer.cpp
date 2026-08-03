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
	// Only attempt formats we declare handleable (handleableMimes() is derived from this same set).
	if ( !VIPS_MIMES.contains( data.mime_name ) ) return std::unexpected( ModuleError { "Unsupported mime type" } );

	VipsModuleSource source { data.file };
	if ( !source.valid() ) return std::unexpected( ModuleError { "Failed to open the file as a vips source" } );

	VipsImage* thumb_raw { nullptr };
	if ( vips_thumbnail_source(
			 source.get(), &thumb_raw, static_cast< int >( width ), "height", static_cast< int >( height ), nullptr )
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