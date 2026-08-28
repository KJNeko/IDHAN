#include "ImageVipsThumbnailer.hpp"

#include <vips/vips.h>

#include <unordered_map>

#include "logging/format_ns.hpp"
#include "vips.hpp"

using namespace idhan;

std::vector< idhan::MimeID > ImageVipsThumbnailer::handleableMimes()
{
	return vipsHandleable();
}

std::expected< ThumbnailInfo, ModuleError > ImageVipsThumbnailer::createThumbnailRaw(
	ModuleCallData& data,
	std::size_t width,
	std::size_t height )
{
	// Only attempt formats we declare handleable (handleableMimes() is derived from this same set).
	if ( !VIPS_MIMES.contains( data.mime_id ) ) return std::unexpected( ModuleError { "Unsupported mime type" } );

	VipsModuleSource source { data.file };
	if ( !source.valid() ) return std::unexpected( ModuleError { "Failed to open the file as a vips source" } );

	const bool force_exact { data.extra.isObject() && data.extra[ "resize_mode" ].isString()
		                     && data.extra[ "resize_mode" ].asString() == "force" };

	VipsImage* thumb_raw { nullptr };
	const int result {
		force_exact ?
			vips_thumbnail_source(
				source.get(),
				&thumb_raw,
				static_cast< int >( width ),
				"height",
				static_cast< int >( height ),
				"size",
				VIPS_SIZE_FORCE,
				nullptr ) :
			vips_thumbnail_source(
				source.get(), &thumb_raw, static_cast< int >( width ), "height", static_cast< int >( height ), nullptr )
	};

	if ( result != 0 ) return std::unexpected( ModuleError { "Failed to generate thumbnail" } );
	VipsImagePtr thumb { thumb_raw };

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

	return idhan::ThumbnailInfo { std::move( thumb ), force_exact ? ThumbnailInfo::NOCACHE : true };
}