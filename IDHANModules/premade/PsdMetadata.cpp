//
// Created by kj16609 on 11/12/25.
//
#include "PsdMetadata.hpp"

#include <vips/vips8>

#include <algorithm>
#include <cstring>

#include "ModuleBase.hpp"

namespace
{

std::uint16_t readUint16BE( const std::uint8_t* data )
{
	return ( static_cast< std::uint16_t >( data[ 0 ] ) << 8 ) | ( static_cast< std::uint16_t >( data[ 1 ] ) << 0 );
}

std::uint32_t readUint32BE( const std::uint8_t* data )
{
	return ( static_cast< std::uint32_t >( data[ 0 ] ) << 24 ) | ( static_cast< std::uint32_t >( data[ 1 ] ) << 16 )
	     | ( static_cast< std::uint32_t >( data[ 2 ] ) << 8 ) | ( static_cast< std::uint32_t >( data[ 3 ] ) << 0 );
}

float readFloat32BE( const std::uint8_t* data )
{
	std::uint32_t bits { readUint32BE( data ) };
	return *reinterpret_cast< float* >( &bits );
}

struct PSDHeader
{
	std::uint16_t channels;
	std::uint32_t height;
	std::uint32_t width;
	std::uint16_t depth;
	std::uint16_t colorMode;
};

std::optional< PSDHeader > parsePSDHeader( const std::uint8_t* data, const std::size_t length )
{
	if ( length < 26 ) return std::nullopt;

	if ( memcmp( data, "8BPS", 4 ) != 0 ) return std::nullopt;

	const std::uint16_t version { readUint16BE( data + 4 ) };
	if ( version != 1 ) return std::nullopt; // TODO: support v2 "large" format

	return { {
		.channels = readUint16BE( data + 12 ),
		.height = readUint32BE( data + 14 ),
		.width = readUint32BE( data + 18 ),
		.depth = readUint16BE( data + 22 ),
		.colorMode = readUint16BE( data + 24 ),
	} };
}

void unpackScanline(
	const std::uint8_t* buffer,
	const std::size_t bufferLength,
	std::uint8_t* output,
	const std::size_t outputLength )
{
	std::size_t inputIdx { 0 };
	std::size_t outputIdx { 0 };

	while ( outputIdx < outputLength && inputIdx < bufferLength )
	{
		const std::uint8_t headerByte { buffer[ inputIdx++ ] };
		if ( headerByte > 128 )
		{
			const std::uint16_t repeatCount { static_cast< std::uint16_t >( 257 - headerByte ) };
			if ( inputIdx >= bufferLength ) break;
			const std::uint8_t value { buffer[ inputIdx++ ] };
			for ( std::uint16_t r = 0; r < repeatCount && outputIdx < outputLength; ++r ) output[ outputIdx++ ] = value;
		}
		else if ( headerByte < 128 )
		{
			const std::uint16_t literalLength { static_cast< std::uint16_t >( headerByte + 1 ) };
			for ( std::uint16_t c = 0; c < literalLength && outputIdx < outputLength && inputIdx < bufferLength; ++c )
				output[ outputIdx++ ] = buffer[ inputIdx++ ];
		}
	}
}

std::vector< std::uint8_t > unpackRaster(
	const std::uint8_t* buffer,
	std::size_t& offset,
	const std::size_t dataLength,
	const std::uint32_t width,
	const std::uint32_t height,
	const std::uint16_t channels )
{
	const std::size_t scanlineCountsSize { height * channels * 2 };
	if ( offset + scanlineCountsSize > dataLength )
	{
		return {};
	}

	std::vector< std::uint16_t > scanlineLengths( height * channels );
	for ( std::size_t i = 0; i < scanlineLengths.size(); ++i )
	{
		scanlineLengths[ i ] = readUint16BE( buffer + offset + i * 2 );
	}
	offset += scanlineCountsSize;

	const std::size_t planeSize { static_cast< std::size_t >( width ) * height };
	std::vector< std::uint8_t > planarData( planeSize * channels );

	std::size_t outputOffset { 0 };
	for ( const std::uint16_t scanlineLength : scanlineLengths )
	{
		const std::uint16_t compressedLength { scanlineLength };

		if ( offset + compressedLength > dataLength )
		{
			return {};
		}

		unpackScanline( buffer + offset, compressedLength, planarData.data() + outputOffset, width );

		offset += compressedLength;
		outputOffset += width;
	}

	return planarData;
}

std::vector< std::uint8_t > convert16to8bit( const std::vector< std::uint8_t >& buffer, const std::size_t pixelCount )
{
	if ( buffer.size() < pixelCount * 2 )
	{
		return {};
	}
	std::vector< std::uint8_t > result( pixelCount );
	for ( std::size_t i = 0; i < pixelCount; ++i )
		result[ i ] = static_cast< std::uint8_t >( readUint16BE( &buffer[ i * 2 ] ) >> 8 );
	return result;
}

std::vector< std::uint8_t > convert32to8bit( const std::vector< std::uint8_t >& buffer, const std::size_t pixelCount )
{
	if ( buffer.size() < pixelCount * 4 )
	{
		return {};
	}
	std::vector< std::uint8_t > result( pixelCount );
	for ( std::size_t i = 0; i < pixelCount; ++i )
		result[ i ] =
			static_cast< std::uint8_t >( std::clamp( readFloat32BE( &buffer[ i * 4 ] ), 0.0f, 1.0f ) * 255.0f );
	return result;
}

std::vector< std::uint8_t > convertToTargetDepth(
	const std::vector< std::uint8_t >& buffer,
	const std::uint16_t depth,
	const std::size_t pixelCount )
{
	switch ( depth )
	{
		case 8:
			return buffer;
		case 16:
			return convert16to8bit( buffer, pixelCount );
		case 32:
			return convert32to8bit( buffer, pixelCount );
		default:
			return {};
	}
}

std::expected< std::vector< std::uint8_t >, idhan::ModuleError > convertCMYKtoInterleavedRGB(
	const std::basic_string_view< std::uint8_t > cmyk,
	const std::size_t pixelCount )
{
	std::vector< std::uint8_t > rgb( pixelCount * 3 );
	for ( std::size_t i = 0; i < pixelCount; ++i )
	{
		const std::uint8_t c { cmyk[ ( pixelCount * 0 ) + i ] };
		const std::uint8_t m { cmyk[ ( pixelCount * 1 ) + i ] };
		const std::uint8_t y { cmyk[ ( pixelCount * 2 ) + i ] };
		const std::uint8_t k { cmyk[ ( pixelCount * 3 ) + i ] };
		// Obviously, this does not take ICC profiles into account, but hopefully it is a good enough first approximation.
		rgb[ i * 3 + 0 ] = static_cast< std::uint8_t >( ( 255 - c ) * ( 255 - k ) / 255 );
		rgb[ i * 3 + 1 ] = static_cast< std::uint8_t >( ( 255 - m ) * ( 255 - k ) / 255 );
		rgb[ i * 3 + 2 ] = static_cast< std::uint8_t >( ( 255 - y ) * ( 255 - k ) / 255 );
	}
	return rgb;
}

std::expected< std::vector< std::uint8_t >, idhan::ModuleError > convertGrayscaleToInterleavedRGB(
	const std::basic_string_view< std::uint8_t > gray,
	const std::size_t pixelCount )
{
	std::vector< std::uint8_t > rgb( pixelCount * 3 );
	for ( std::size_t i = 0; i < pixelCount; ++i )
	{
		const std::uint8_t value { gray[ i ] };
		rgb[ i * 3 + 0 ] = value;
		rgb[ i * 3 + 1 ] = value;
		rgb[ i * 3 + 2 ] = value;
	}
	return rgb;
}

std::expected< std::vector< std::uint8_t >, idhan::ModuleError > convertIndexedToInterleavedRGB(
	const std::basic_string_view< std::uint8_t > indexed,
	const std::size_t pixelCount,
	const std::basic_string_view< std::uint8_t > colorTable )
{
	if ( colorTable.length() < 0x300 )
	{
		return std::unexpected( idhan::ModuleError { "Short color table" } );
	}
	std::vector< std::uint8_t > rgb( pixelCount * 3 );
	for ( std::size_t i = 0; i < pixelCount; ++i )
	{
		const std::uint8_t value { indexed[ i ] };
		rgb[ i * 3 + 0 ] = colorTable[ value + 0x000 ];
		rgb[ i * 3 + 1 ] = colorTable[ value + 0x100 ];
		rgb[ i * 3 + 2 ] = colorTable[ value + 0x200 ];
	}
	return rgb;
}

std::expected< std::vector< std::uint8_t >, idhan::ModuleError > convertPlanarRGBToInterleavedRGB(
	const std::basic_string_view< std::uint8_t > planarData,
	const std::size_t pixelCount,
	const std::uint16_t channels )
{
	std::vector< std::uint8_t > interleaved( pixelCount * 3 );
	for ( std::size_t c = 0; c < std::min< std::size_t >( channels, 3 ); ++c )
	{
		const std::uint8_t* planeData { planarData.data() + ( c * pixelCount ) };
		for ( std::size_t i = 0; i < pixelCount; ++i )
		{
			interleaved[ i * 3 + c ] = planeData[ i ];
		}
	}
	return interleaved;
}

std::expected< std::vector< std::uint8_t >, idhan::ModuleError > convertPlanarToInterleavedRGB(
	const std::basic_string_view< std::uint8_t > planarData,
	const std::uint16_t colorMode,
	const std::uint32_t width,
	const std::uint32_t height,
	const std::uint16_t channels,
	const std::basic_string_view< std::uint8_t > colorTable )
{
	const std::size_t pixelCount { static_cast< std::size_t >( width ) * height };

	switch ( colorMode )
	{
		case 1: // Grayscale
			return convertGrayscaleToInterleavedRGB( planarData, pixelCount );
		case 2: // Indexed
			return convertIndexedToInterleavedRGB( planarData, pixelCount, colorTable );
		case 3: // RGB
			return convertPlanarRGBToInterleavedRGB( planarData, pixelCount, channels );
		case 4: // CMYK
			return convertCMYKtoInterleavedRGB( planarData, pixelCount );
		default:
			return std::unexpected( idhan::ModuleError { "Unsupported color mode" } );
	}
}

std::uint32_t countPSDLayers( const std::uint8_t* data, std::size_t length )
{
	// Start past the file header.
	std::size_t offset { 26 };

	// Then, skip the color mode data.
	if ( offset + 4 > length ) return 0;
	const std::uint32_t colorModeLength { readUint32BE( data + offset ) };
	offset += 4 + colorModeLength;

	// Next, skip image resources section.
	if ( offset + 4 > length ) return 0;
	const std::uint32_t resourcesLength { readUint32BE( data + offset ) };
	offset += 4 + resourcesLength;

	// Read layer and mask info
	if ( offset + 4 > length ) return 0;
	const std::uint32_t layerMaskLength { readUint32BE( data + offset ) };
	offset += 4;
	if ( layerMaskLength == 0 ) return 0;
	const std::size_t layerMaskEnd { offset + layerMaskLength };
	if ( layerMaskEnd > length ) return 0;

	// Read layer info length
	if ( offset + 4 > length ) return 0;
	const std::uint32_t layerInfoLength { readUint32BE( data + offset ) };
	offset += 4;
	if ( layerInfoLength == 0 ) return 0;

	// Read layer count
	if ( offset + 2 > length ) return 0;
	const std::int16_t rawLayerCount { static_cast< int16_t >( readUint16BE( data + offset ) ) };
	offset += 2;

	const std::uint16_t layerCount { std::abs< std::uint16_t >( rawLayerCount ) };
	std::uint32_t realLayerCount = 0;

	// Parse each layer to determine groupType
	for ( std::uint16_t i = 0; i < layerCount; ++i )
	{
		// Skip bounds (top, left, bottom, right)
		if ( offset + 16 > length ) return realLayerCount;
		offset += 16;

		// Read channel count
		if ( offset + 2 > length ) return realLayerCount;
		const std::uint16_t channelCount { readUint16BE( data + offset ) };
		offset += 2;

		// Skip channel info
		if ( offset + channelCount * 6 > length ) return realLayerCount;
		offset += channelCount * 6;

		// Skip signature, blend mode, opacity, clipping, flags, filler
		if ( offset + 12 > length ) return realLayerCount;
		offset += 12;

		// Read extra data length
		if ( offset + 4 > length ) return realLayerCount;
		const std::uint32_t extraDataLength { readUint32BE( data + offset ) };
		offset += 4;

		const std::size_t extraDataEnd { offset + extraDataLength };
		if ( extraDataEnd > length ) return realLayerCount;

		// Skip mask data length + mask data
		if ( offset + 4 > length ) return realLayerCount;
		const std::uint32_t maskLength { readUint32BE( data + offset ) };
		offset += 4 + maskLength;

		// Skip blending ranges
		if ( offset + 4 > length ) return realLayerCount;
		const std::uint32_t blendingRangesLength { readUint32BE( data + offset ) };
		offset += 4 + blendingRangesLength;

		// Skip layer name
		if ( offset >= length ) return realLayerCount;
		const std::uint8_t nameLen { data[ offset ] };
		offset += 1 + ( ( nameLen + 1 + 3 ) & ~3 ); // Padded to 4 bytes

		// Parse additional layer info to find "lsct" (layer section divider)
		std::uint32_t groupType { 0 }; // Default to NORMA}

		while ( offset + 12 <= extraDataEnd && offset + 12 <= length )
		{
			if ( memcmp( data + offset, "8BIM", 4 ) != 0 && memcmp( data + offset, "8B64", 4 ) != 0 ) break;
			offset += 4;

			char key[ 5 ] = { 0 };
			memcpy( key, data + offset, 4 );
			offset += 4;

			const std::uint32_t dataLength { readUint32BE( data + offset ) };
			offset += 4;

			const std::size_t dataEnd = offset + dataLength;
			if ( dataEnd > extraDataEnd || dataEnd > length ) break;

			if ( memcmp( key, "lsct", 4 ) == 0 && dataLength >= 4 )
			{
				groupType = readUint32BE( data + offset );
			}

			offset = dataEnd;
		}

		// Move to end of extra data
		offset = extraDataEnd;

		if ( groupType != 3 ) // Ignore layer section dividers.
		{
			realLayerCount++;
		}
	}

	return realLayerCount;
}

} // namespace

std::vector< std::string_view > PsdMetadata::handleableMimes()
{
	return { "application/psd" };
}

std::string_view PsdMetadata::name()
{
	return "PSD Metadata Parser";
}

idhan::ModuleVersion PsdMetadata::version()
{
	return { .m_major = 1, .m_minor = 0, .m_patch = 0 };
}

std::expected< idhan::MetadataInfo, idhan::ModuleError > PsdMetadata::parseFile(
	const void* data,
	const std::size_t length,
	[[maybe_unused]] std::string mime_name )
{
	const auto* bytes { static_cast< const std::uint8_t* >( data ) };

	const auto header { parsePSDHeader( bytes, length ) };
	if ( !header )
	{
		return std::unexpected( idhan::ModuleError { "Invalid PSD header" } );
	}

	idhan::MetadataInfo generic_metadata {};
	idhan::MetadataInfoImageProject project_metadata {};

	project_metadata.image_info.width = static_cast< int >( header->width );
	project_metadata.image_info.height = static_cast< int >( header->height );
	project_metadata.image_info.channels = static_cast< std::uint8_t >( header->channels );
	project_metadata.layers = countPSDLayers( bytes, length );

	generic_metadata.m_simple_type = idhan::SimpleMimeType::IMAGE_PROJECT;
	generic_metadata.m_metadata = project_metadata;

	return generic_metadata;
}

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

std::expected< idhan::ThumbnailerModuleI::ThumbnailInfo, idhan::ModuleError > PsdThumbnailer::createThumbnail(
	const void* data,
	const std::size_t length,
	std::size_t width,
	std::size_t height,
	[[maybe_unused]] std::string mime_name )
{
	const auto bytes { static_cast< const std::uint8_t* >( data ) };

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
				planarData = unpackRaster( bytes, offset, length, header->width, header->height, header->channels );
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

	VipsImage* image { vips_image_new_from_memory(
		interleavedRGB->data(),
		interleavedRGB->size(),
		static_cast< int >( header->width ),
		static_cast< int >( header->height ),
		3,
		VIPS_FORMAT_UCHAR ) };

	if ( !image )
	{
		return std::unexpected( idhan::ModuleError { "Failed to create image from PSD data" } );
	}

	const float source_aspect { static_cast< float >( header->width ) / static_cast< float >( header->height ) };
	const float target_aspect { static_cast< float >( width ) / static_cast< float >( height ) };

	if ( target_aspect > source_aspect )
		width = static_cast< std::size_t >( static_cast< float >( height ) * source_aspect );
	else
		height = static_cast< std::size_t >( static_cast< float >( width ) / source_aspect );

	VipsImage* resized { nullptr };
	if ( vips_resize(
			 image,
			 &resized,
			 static_cast< double >( width ) / static_cast< double >( vips_image_get_width( image ) ),
			 nullptr ) )
	{
		g_object_unref( image );
		return std::unexpected( idhan::ModuleError { "Failed to resize image" } );
	}
	g_object_unref( image );

	void* output_buffer { nullptr };
	std::size_t output_length { 0 };
	if ( vips_pngsave_buffer( resized, &output_buffer, &output_length, nullptr ) )
	{
		g_object_unref( resized );
		return std::unexpected( idhan::ModuleError { "Failed to save thumbnail" } );
	}
	g_object_unref( resized );

	std::vector< std::byte > output(
		static_cast< std::byte* >( output_buffer ), static_cast< std::byte* >( output_buffer ) + output_length );
	g_free( output_buffer );

	ThumbnailInfo info {};
	info.data = std::move( output );
	info.width = width;
	info.height = height;

	return info;
}
