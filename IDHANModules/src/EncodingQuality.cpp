#include "EncodingQuality.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#include "MimeIDs.hpp"
#include "ModuleFile.hpp"

namespace idhan
{

//! Annex K.1, the standard luminance table, in natural order.
constexpr QuantizationTable STANDARD_LUMA {
	{ 16, 11,  10,  16, 24, 40, 51, 61, 12,  12,  14,  19,  26, 58, 60, 55,  14,  13,  16,  24, 40, 57,
	  69, 56,  14,  17, 22, 29, 51, 87, 80,  62,  18,  22,  37, 56, 68, 109, 103, 77,  24,  35, 55, 64,
	  81, 104, 113, 92, 49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98,  112, 100, 103, 99 }
};

//! Annex K.2, the standard chrominance table, in natural order.
constexpr QuantizationTable STANDARD_CHROMA {
	{ 17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99,
	  99, 99, 47, 66, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
	  99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99 }
};

//! Map zigzag positions to natural-order indices, matching libjpeg's jpeg_natural_order.
constexpr std::array< std::uint8_t, 64 > ZIGZAG {
	{ 0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
	  41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
	  30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63 }
};

//! Maximum JPEG header scan size; allows large metadata segments while bounding reads.
constexpr std::size_t JPEG_SCAN_LIMIT { 8 * 1024 * 1024 };

//! Return nullopt on a failed or short read.
static std::optional< std::vector< std::byte > > readAt(
	const ModuleFile& file,
	const std::size_t offset,
	const std::size_t length )
{
	if ( offset > file.size() || file.size() - offset < length ) return std::nullopt;
	if ( length == 0 ) return std::vector< std::byte > {};

	std::vector< std::byte > buffer( length );
	const auto read { file.read( buffer, offset ) };
	if ( !read || *read != length ) return std::nullopt;

	return buffer;
}

static std::uint16_t bigEndian16( const std::span< const std::byte > bytes, const std::size_t offset )
{
	return static_cast< std::uint16_t >(
		( static_cast< std::uint16_t >( bytes[ offset ] ) << 8 )
		| static_cast< std::uint16_t >( bytes[ offset + 1 ] ) );
}

//! libjpeg's jpeg_quality_scaling: the percentage every standard table entry is scaled by.
static int scaleFactor( const int quality )
{
	return quality < 50 ? 5000 / quality : 200 - ( quality * 2 );
}

//! Match libjpeg's jpeg_add_quant_table with force_baseline enabled.
static std::uint16_t scaleEntry( const std::uint16_t base, const int scale )
{
	const long scaled { ( ( static_cast< long >( base ) * scale ) + 50 ) / 100 };
	return static_cast< std::uint16_t >( std::clamp< long >( scaled, 1, 255 ) );
}

std::uint8_t estimateJpegQuality( const QuantizationTable& luma, const std::optional< QuantizationTable > chroma )
{
	std::uint8_t best { 1 };
	long best_error { std::numeric_limits< long >::max() };

	for ( int quality = 1; quality <= 100; ++quality )
	{
		const int scale { scaleFactor( quality ) };
		long error { 0 };

		for ( std::size_t position = 0; position < luma.size(); ++position )
		{
			const auto natural { ZIGZAG[ position ] };
			error +=
				std::abs( static_cast< long >( luma[ position ] ) - scaleEntry( STANDARD_LUMA[ natural ], scale ) );
			if ( chroma )
				error += std::abs(
					static_cast< long >( ( *chroma )[ position ] ) - scaleEntry( STANDARD_CHROMA[ natural ], scale ) );
		}

		if ( error >= best_error ) continue;

		best_error = error;
		best = static_cast< std::uint8_t >( quality );
	}

	return best;
}

//! Quantization tables indexed by the identifiers used by frame components.
using QuantizationTables = std::array< std::optional< QuantizationTable >, 4 >;

static void readQuantizationTables( const std::span< const std::byte > payload, QuantizationTables& tables )
{
	std::size_t position { 0 };

	while ( position < payload.size() )
	{
		const auto header { static_cast< std::uint8_t >( payload[ position ] ) };
		++position;

		const std::uint8_t precision { static_cast< std::uint8_t >( header >> 4 ) };
		const std::uint8_t id { static_cast< std::uint8_t >( header & 0x0F ) };
		const std::size_t entry_bytes { precision == 0 ? 1u : 2u };
		const std::size_t table_bytes { 64 * entry_bytes };

		if ( id >= tables.size() || payload.size() - position < table_bytes ) return;

		QuantizationTable table {};
		for ( std::size_t entry = 0; entry < table.size(); ++entry )
		{
			table[ entry ] = precision == 0 ? static_cast< std::uint16_t >( payload[ position + entry ] ) :
			                                  bigEndian16( payload, position + ( entry * 2 ) );
		}

		tables[ id ] = table;
		position += table_bytes;
	}
}

//! Component 0 carries the luma plane, and the chroma planes are sampled against it.
static ChromaSubsampling readSubsampling( const std::span< const std::byte > frame )
{
	constexpr std::size_t COMPONENT_COUNT_OFFSET { 5 };
	constexpr std::size_t FIRST_COMPONENT_OFFSET { 6 };
	constexpr std::size_t COMPONENT_BYTES { 3 };

	if ( frame.size() <= COMPONENT_COUNT_OFFSET ) return ChromaSubsampling::UNKNOWN;

	const auto components { static_cast< std::uint8_t >( frame[ COMPONENT_COUNT_OFFSET ] ) };
	if ( components == 1 ) return ChromaSubsampling::GREYSCALE;
	if ( components < 3 ) return ChromaSubsampling::UNKNOWN;
	if ( frame.size() < FIRST_COMPONENT_OFFSET + ( COMPONENT_BYTES * components ) ) return ChromaSubsampling::UNKNOWN;

	const auto sampling = [ frame ]( const std::size_t component )
	{ return static_cast< std::uint8_t >( frame[ FIRST_COMPONENT_OFFSET + ( COMPONENT_BYTES * component ) + 1 ] ); };

	const std::uint8_t luma { sampling( 0 ) };
	const std::uint8_t chroma { sampling( 1 ) };
	const std::uint8_t horizontal { static_cast< std::uint8_t >( chroma >> 4 ) };
	const std::uint8_t vertical { static_cast< std::uint8_t >( chroma & 0x0F ) };

	if ( horizontal == 0 || vertical == 0 ) return ChromaSubsampling::UNKNOWN;

	const int across { ( luma >> 4 ) / horizontal };
	const int down { ( luma & 0x0F ) / vertical };

	if ( across == 1 && down == 1 ) return ChromaSubsampling::NONE;
	if ( across == 2 && down == 1 ) return ChromaSubsampling::HORIZONTAL;
	if ( across == 1 && down == 2 ) return ChromaSubsampling::VERTICAL;
	if ( across == 2 && down == 2 ) return ChromaSubsampling::BOTH;

	return ChromaSubsampling::UNKNOWN;
}

/**
 * Remove NUL, control characters, and invalid UTF-8 before storing COM text in PostgreSQL or
 * JSON.
 */
static std::string sanitizeComment( const std::span< const std::byte > payload )
{
	std::string text {};
	text.reserve( std::min( payload.size(), COMMENT_LIMIT ) );

	for ( std::size_t index = 0; index < payload.size() && text.size() < COMMENT_LIMIT; )
	{
		const auto lead { static_cast< std::uint8_t >( payload[ index ] ) };

		if ( lead < 0x20 || lead == 0x7F )
		{
			// Preserve whitespace separators as spaces.
			if ( lead == '\t' || lead == '\n' || lead == '\r' ) text.push_back( ' ' );
			++index;
			continue;
		}

		if ( lead < 0x80 )
		{
			text.push_back( static_cast< char >( lead ) );
			++index;
			continue;
		}

		// 0xC0 and 0xC1 start invalid, overlong UTF-8 sequences.
		const std::size_t width { lead >= 0xF0 ? 4u : lead >= 0xE0 ? 3u : lead >= 0xC2 ? 2u : 0u };
		if ( width == 0 || payload.size() - index < width )
		{
			++index;
			continue;
		}

		if ( text.size() + width > COMMENT_LIMIT ) break;

		bool valid { true };
		for ( std::size_t byte = 1; byte < width; ++byte )
			valid = valid && ( static_cast< std::uint8_t >( payload[ index + byte ] ) & 0xC0 ) == 0x80;

		if ( !valid )
		{
			++index;
			continue;
		}

		for ( std::size_t byte = 0; byte < width; ++byte )
			text.push_back( static_cast< char >( payload[ index + byte ] ) );
		index += width;
	}

	const auto first { text.find_first_not_of( ' ' ) };
	if ( first == std::string::npos ) return {};

	return text.substr( first, text.find_last_not_of( ' ' ) - first + 1 );
}

//! SOF markers exclude DHT, JPG, and DAC within the 0xC0-0xCF range.
static bool isFrameMarker( const std::uint8_t marker )
{
	if ( marker < 0xC0 || marker > 0xCF ) return false;
	return marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
}

//! Lossless SOF modes do not use quantization-based quality estimates.
static bool isLosslessFrameMarker( const std::uint8_t marker )
{
	return marker == 0xC3 || marker == 0xC7 || marker == 0xCB || marker == 0xCF;
}

static FileEncoding parseJpeg( const ModuleFile& file )
{
	FileEncoding result {};
	const auto start { readAt( file, 0, 2 ) };
	if ( !start || ( *start )[ 0 ] != std::byte { 0xFF } || ( *start )[ 1 ] != std::byte { 0xD8 } ) return result;

	QuantizationTables tables {};
	EncodingQuality encoding {};
	// Initialize parsed JPEG comments as empty; unsupported formats retain nullopt.
	std::string comment {};
	bool framed { false };
	bool lossless_frame { false };
	std::size_t offset { 2 };

	while ( offset < JPEG_SCAN_LIMIT )
	{
		const auto head { readAt( file, offset, 2 ) };
		if ( !head || ( *head )[ 0 ] != std::byte { 0xFF } ) break;

		const auto marker { static_cast< std::uint8_t >( ( *head )[ 1 ] ) };

		// JPEG permits repeated 0xFF padding bytes before a marker.
		if ( marker == 0xFF )
		{
			offset += 1;
			continue;
		}

		// SOI, TEM, and restart markers have no length field.
		if ( marker == 0xD8 || marker == 0x01 || ( marker >= 0xD0 && marker <= 0xD7 ) )
		{
			offset += 2;
			continue;
		}

		// Stop header parsing at SOS or EOI.
		if ( marker == 0xDA || marker == 0xD9 ) break;

		const auto header { readAt( file, offset + 2, 2 ) };
		if ( !header ) break;

		const std::size_t length { bigEndian16( *header, 0 ) };
		if ( length < 2 ) break;

		const std::size_t payload_offset { offset + 4 };
		const std::size_t payload_length { length - 2 };

		if ( marker == 0xDB )
		{
			const auto payload { readAt( file, payload_offset, payload_length ) };
			if ( !payload ) break;
			readQuantizationTables( *payload, tables );
		}
		else if ( marker == 0xFE )
		{
			// Retain the first COM segment.
			const auto payload { readAt( file, payload_offset, payload_length ) };
			if ( !payload ) break;
			if ( comment.empty() ) comment = sanitizeComment( *payload );
		}
		else if ( isFrameMarker( marker ) )
		{
			framed = true;
			lossless_frame = isLosslessFrameMarker( marker );
			if ( lossless_frame ) break;

			const auto payload { readAt( file, payload_offset, payload_length ) };
			if ( !payload ) break;

			encoding.subsampling = readSubsampling( *payload );
			encoding.progressive = marker == 0xC2 || marker == 0xC6 || marker == 0xCA || marker == 0xCE;
		}

		offset = payload_offset + payload_length;
	}

	result.comment = std::move( comment );

	if ( lossless_frame )
	{
		// Do not estimate quantization quality for lossless frames.
		result.quality = EncodingQuality { .lossless = true };
		return result;
	}

	if ( !framed && !tables[ 0 ] ) return result;

	if ( tables[ 0 ] ) encoding.quality = estimateJpegQuality( *tables[ 0 ], tables[ 1 ] );
	result.quality = encoding;

	return result;
}

//! Report only lossless status for WebP; this parser does not decode VP8 quantizer settings.
static std::optional< EncodingQuality > parseWebp( const ModuleFile& file )
{
	constexpr std::size_t RIFF_HEADER { 12 };
	constexpr std::size_t CHUNK_HEADER { 8 };
	//! Extended WebP may place VP8X, alpha, or metadata chunks before the image chunk.
	constexpr std::size_t CHUNK_SCAN_LIMIT { 16 };

	const auto riff { readAt( file, 0, RIFF_HEADER ) };
	if ( !riff ) return std::nullopt;

	const std::span< const std::byte > header { *riff };
	const auto tag = []( const std::span< const std::byte > bytes, const std::size_t offset, const char* name )
	{
		for ( std::size_t index = 0; index < 4; ++index )
			if ( bytes[ offset + index ] != static_cast< std::byte >( name[ index ] ) ) return false;
		return true;
	};

	if ( !tag( header, 0, "RIFF" ) || !tag( header, 8, "WEBP" ) ) return std::nullopt;

	std::size_t offset { RIFF_HEADER };

	for ( std::size_t chunk = 0; chunk < CHUNK_SCAN_LIMIT; ++chunk )
	{
		const auto chunk_header { readAt( file, offset, CHUNK_HEADER ) };
		if ( !chunk_header ) return std::nullopt;

		const std::span< const std::byte > bytes { *chunk_header };
		if ( tag( bytes, 0, "VP8L" ) ) return EncodingQuality { .lossless = true };
		if ( tag( bytes, 0, "VP8 " ) ) return EncodingQuality { .lossless = false };

		std::size_t size { 0 };
		for ( std::size_t index = 0; index < 4; ++index )
			size |= static_cast< std::size_t >( bytes[ 4 + index ] ) << ( index * 8 );

		// Chunks are padded to an even length, and the pad byte is not counted in the size.
		offset += CHUNK_HEADER + size + ( size % 2 );
	}

	return std::nullopt;
}

FileEncoding readFileEncoding( const ModuleFile& file, const MimeID mime_id )
{
	switch ( mime_id )
	{
		case mime_ids::IMAGE_JPEG:
			return parseJpeg( file );
		case mime_ids::IMAGE_WEBP:
			return FileEncoding { .quality = parseWebp( file ) };
		case mime_ids::IMAGE_PNG:
			[[fallthrough]];
		case mime_ids::ANIMATION_APNG:
			[[fallthrough]];
		case mime_ids::ANIMATION_GIF:
			// PNG text chunks are not parsed, so comment remains unknown.
			return FileEncoding { .quality = EncodingQuality { .lossless = true } };
		default:
			// Encoding quality is unsupported for TIFF and AVIF.
			return {};
	}
}

} // namespace idhan
