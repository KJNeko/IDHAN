#include "EmbeddedMetadata.hpp"

#include <vips/vips.h>

#include <string_view>

namespace idhan
{

static bool hasField( VipsImage* const image, const char* const name )
{
	return vips_image_get_typeof( image, name ) != 0;
}

//! Whether the EXIF block carries a GPS directory. libvips decodes each tag into a field named
//! `exif-ifd<n>-<tag>`, where IFD 3 is GPS.
static bool hasExifGps( VipsImage* const image )
{
	gchar** const fields { vips_image_get_fields( image ) };
	if ( fields == nullptr ) return false;

	bool found { false };
	for ( gchar** field = fields; ( *field != nullptr ) && !found; ++field )
		found = std::string_view( *field ).starts_with( "exif-ifd3-" );

	g_strfreev( fields );

	return found;
}

EmbeddedMetadata detectEmbeddedMetadata( VipsImage* const image )
{
	if ( image == nullptr ) return {};

	const bool exif { hasField( image, VIPS_META_EXIF_NAME ) };

	return EmbeddedMetadata {
		.exif = exif,
		.gps = exif && hasExifGps( image ),
		.xmp = hasField( image, VIPS_META_XMP_NAME ),
		.iptc = hasField( image, VIPS_META_IPTC_NAME ),
		.icc_profile = hasField( image, VIPS_META_ICC_NAME )
	};
}

} // namespace idhan
