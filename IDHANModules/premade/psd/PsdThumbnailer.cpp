#include "PsdThumbnailer.hpp"

#include <vips/vips.h>

#include <string>

#include "psd.hpp"
#include "vips.hpp"

std::vector< std::string_view > PsdThumbnailer::handleableMimes()
{
	return { "application/psd" };
}

std::string_view PsdThumbnailer::name()
{
	return "PSD Thumbnailer Parser";
}

idhan::ModuleVersion PsdThumbnailer::version()
{
	return { .m_major = 1, .m_minor = 0, .m_patch = 0 };
}

using namespace psd;

std::expected< idhan::ThumbnailInfo, idhan::ModuleError > PsdThumbnailer::createThumbnailRaw(
	idhan::ModuleCallData& data,
	std::size_t width,
	std::size_t height )
{
	const auto contents { readWholeFile( data.file ) };
	if ( !contents ) return std::unexpected( contents.error() );

	const auto* bytes { contents->data() };
	const auto length { contents->size() };

	const auto header { parsePSDHeader( bytes, length ) };
	if ( !header )
	{
		return std::unexpected( idhan::ModuleError { "Invalid PSD header" } );
	}

	if ( header->depth != 8 && header->depth != 16 && header->depth != 32 )
	{
		return std::unexpected( idhan::ModuleError { "Unsupported bit depth" } );
	}

	std::size_t offset { 26 };

	if ( offset + 4 > length ) return std::unexpected( idhan::ModuleError { "Truncated file" } );
	const std::uint32_t colorModeLength { readUint32BE( bytes + offset ) };
	offset += 4;

	if ( offset + colorModeLength > length ) return std::unexpected( idhan::ModuleError { "Truncated file" } );
	const std::basic_string_view colorTable { bytes + offset, colorModeLength };
	offset += colorModeLength;

	if ( offset + 4 > length ) return std::unexpected( idhan::ModuleError { "Truncated file" } );
	const std::uint32_t resourcesLength { readUint32BE( bytes + offset ) };
	offset += 4 + resourcesLength;

	if ( offset + 4 > length ) return std::unexpected( idhan::ModuleError { "Truncated file" } );
	const std::uint32_t layerMaskLength { readUint32BE( bytes + offset ) };
	offset += 4 + layerMaskLength;

	if ( offset + 2 > length ) return std::unexpected( idhan::ModuleError { "Truncated file" } );
	const std::uint16_t compression { readUint16BE( bytes + offset ) };
	offset += 2;

	const std::size_t bytesPerSample { static_cast< std::size_t >( header->depth / 8 ) };
	const std::size_t planeSize { static_cast< std::size_t >( header->width ) * header->height };
	std::vector< std::uint8_t > planarData;

	switch ( compression )
	{
		case 0: // Uncompressed
			{
				std::size_t expectedSize { planeSize * header->channels * bytesPerSample };

				if ( offset + expectedSize > length )
				{
					return std::unexpected( idhan::ModuleError { "Insufficient image data" } );
				}

				planarData.assign( bytes + offset, bytes + offset + expectedSize );
			}
			break;
		case 1: // PackBits
			{
				planarData = unpackRaster(
					bytes, offset, length, header->width, header->height, header->channels, bytesPerSample );
				if ( planarData.empty() )
				{
					return std::unexpected( idhan::ModuleError { "Failed to decompress RLE data" } );
				}
			}
			break;
		default:
			return std::unexpected( idhan::ModuleError { "Unsupported compression method" } );
	}

	std::size_t totalPixels { planeSize * header->channels };
	std::vector< std::uint8_t > planar8bit { convertToTargetDepth( planarData, header->depth, totalPixels ) };
	if ( planar8bit.empty() )
	{
		return std::unexpected( idhan::ModuleError { "Failed to convert bit depth" } );
	}

	auto interleavedRGB { convertPlanarToInterleavedRGB(
		std::basic_string_view( planar8bit.data(), planar8bit.size() ),
		header->colorMode,
		header->width,
		header->height,
		header->channels,
		colorTable ) };

	if ( !interleavedRGB.has_value() )
	{
		return std::unexpected( interleavedRGB.error() );
	}

	idhan::VipsImagePtr image { vips_image_new_from_memory(
		interleavedRGB->data(),
		interleavedRGB->size(),
		static_cast< int >( header->width ),
		static_cast< int >( header->height ),
		3,
		VIPS_FORMAT_UCHAR ) };
	if ( !image ) return std::unexpected( idhan::ModuleError { "Failed to create image from PSD data" } );

	const float source_aspect { static_cast< float >( header->width ) / static_cast< float >( header->height ) };
	const float target_aspect { static_cast< float >( width ) / static_cast< float >( height ) };

	if ( target_aspect > source_aspect )
		width = static_cast< std::size_t >( static_cast< float >( height ) * source_aspect );
	else
		height = static_cast< std::size_t >( static_cast< float >( width ) / source_aspect );

	VipsImage* resized_raw { nullptr };
	if ( vips_resize(
			 image.get(),
			 &resized_raw,
			 static_cast< double >( width ) / static_cast< double >( vips_image_get_width( image.get() ) ),
			 nullptr )
	     != 0 )
		return std::unexpected( idhan::ModuleError { "Failed to resize PSD image" } );
	idhan::VipsImagePtr resized { resized_raw };

	return idhan::ThumbnailInfo { std::move( resized ) };
}
