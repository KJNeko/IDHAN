#include "psd.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <format>
#include <memory>
#include <span>

namespace psd
{

std::expected< std::vector< std::uint8_t >, idhan::ModuleError > readWholeFile( const idhan::ModuleFile& file )
{
	std::vector< std::uint8_t > buffer( file.size() );

	std::size_t filled { 0 };
	while ( filled < buffer.size() )
	{
		const auto count { file.read(
			std::span< std::byte > { reinterpret_cast< std::byte* >( buffer.data() ) + filled, buffer.size() - filled },
			filled ) };

		if ( !count ) return std::unexpected( count.error() );

		// The file ended before it said it would. Carrying on would parse zeroes as PSD structure.
		if ( *count == 0 )
			return std::unexpected(
				idhan::ModuleError { std::format( "PSD ended at {} of {} bytes", filled, buffer.size() ) } );

		filled += *count;
	}

	return buffer;
}

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
	const std::uint32_t bits { readUint32BE( data ) };
	return std::bit_cast< float >( bits );
}

std::optional< PSDHeader > parsePSDHeader( const std::uint8_t* data, const std::size_t length )
{
	if ( length < 26 ) return std::nullopt;

	if ( std::memcmp( data, "8BPS", 4 ) != 0 ) return std::nullopt;

	const std::uint16_t version { readUint16BE( data + 4 ) };
	if ( version != 1 ) return std::nullopt; // TODO: support v2 "large" format

	const std::uint16_t channels { readUint16BE( data + 12 ) };
	const std::uint32_t height { readUint32BE( data + 14 ) };
	const std::uint32_t width { readUint32BE( data + 18 ) };

	// PSD (version 1) constrains each dimension to 1..30000 and the channel count to 1..56.
	// Rejecting out-of-range headers stops a crafted file from driving a multi-gigabyte
	// allocation (planeSize = width * height) downstream, and rules out the width/height == 0
	// that would otherwise divide by zero in the aspect-ratio maths.
	constexpr std::uint32_t max_dimension { 30000 };
	constexpr std::uint16_t max_channels { 56 };
	if ( width == 0 || height == 0 || width > max_dimension || height > max_dimension ) return std::nullopt;
	if ( channels == 0 || channels > max_channels ) return std::nullopt;

	return { {
		.channels = channels,
		.height = height,
		.width = width,
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
	const std::uint16_t channels,
	const std::size_t bytesPerSample )
{
	const std::size_t scanlineCountsSize { height * channels * 2 };
	std::vector< std::uint8_t > planarData {};
	if ( offset + scanlineCountsSize > dataLength )
	{
		return planarData;
	}

	std::vector< std::uint16_t > scanlineLengths( height * channels );
	for ( std::size_t i = 0; i < scanlineLengths.size(); ++i )
	{
		scanlineLengths[ i ] = readUint16BE( buffer + offset + i * 2 );
	}
	offset += scanlineCountsSize;

	// Each row decompresses to width * bytesPerSample bytes (PackBits runs over the raw sample
	// bytes, not whole pixels), so both the row stride and the plane buffer scale with
	// bytesPerSample. Ignoring it truncated every 16/32-bit RLE PSD to a width-sized buffer and
	// made the subsequent depth conversion reject the data.
	const std::size_t rowBytes { static_cast< std::size_t >( width ) * bytesPerSample };
	const std::size_t planeSize { static_cast< std::size_t >( width ) * height };
	planarData.resize( planeSize * channels * bytesPerSample );

	std::size_t outputOffset { 0 };
	for ( const std::uint16_t scanlineLength : scanlineLengths )
	{
		const std::uint16_t compressedLength { scanlineLength };

		if ( offset + compressedLength > dataLength )
		{
			return planarData;
		}

		unpackScanline( buffer + offset, compressedLength, planarData.data() + outputOffset, rowBytes );

		offset += compressedLength;
		outputOffset += rowBytes;
	}

	return planarData;
}

std::vector< std::uint8_t > convert16to8bit( const std::vector< std::uint8_t >& buffer, const std::size_t pixelCount )
{
	std::vector< std::uint8_t > result {};
	if ( buffer.size() < pixelCount * 2 )
	{
		return result;
	}

	result.resize( pixelCount );

	for ( std::size_t i = 0; i < pixelCount; ++i )
		result[ i ] = static_cast< std::uint8_t >( readUint16BE( &buffer[ i * 2 ] ) >> 8 );
	return result;
}

std::vector< std::uint8_t > convert32to8bit( const std::vector< std::uint8_t >& buffer, const std::size_t pixelCount )
{
	std::vector< std::uint8_t > result {};
	if ( buffer.size() < pixelCount * 4 )
	{
		return result;
	}

	result.resize( pixelCount );

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
	// need four planes (C, M, Y, K) of pixelCount bytes each; division form avoids overflow
	if ( pixelCount > cmyk.size() / 4 ) return std::unexpected( idhan::ModuleError { "CMYK plane data too small" } );

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
	if ( gray.size() < pixelCount ) return std::unexpected( idhan::ModuleError { "Grayscale plane data too small" } );

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
	if ( indexed.size() < pixelCount ) return std::unexpected( idhan::ModuleError { "Indexed plane data too small" } );
	std::vector< std::uint8_t > rgb( pixelCount * 3 );
	for ( std::size_t i = 0; i < pixelCount; ++i )
	{
		const std::uint8_t value { indexed[ i ] };
		rgb[ i * 3 + 0 ] = colorTable[ static_cast< std::size_t >( value ) + 0x000 ];
		rgb[ i * 3 + 1 ] = colorTable[ static_cast< std::size_t >( value ) + 0x100 ];
		rgb[ i * 3 + 2 ] = colorTable[ static_cast< std::size_t >( value ) + 0x200 ];
	}
	return rgb;
}

std::expected< std::vector< std::uint8_t >, idhan::ModuleError > convertPlanarRGBToInterleavedRGB(
	const std::basic_string_view< std::uint8_t > planarData,
	const std::size_t pixelCount,
	const std::uint16_t channels )
{
	const std::size_t planes { std::min< std::size_t >( channels, 3 ) };
	if ( planes != 0 && planarData.size() / planes < pixelCount )
		return std::unexpected( idhan::ModuleError { "Planar RGB plane data too small" } );

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

	const std::uint16_t layerCount { static_cast< std::uint16_t >( std::abs( rawLayerCount ) ) };
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
		if ( offset + static_cast< std::size_t >( channelCount ) * 6 > length ) return realLayerCount;
		offset += static_cast< std::size_t >( channelCount ) * 6;

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

} // namespace psd
