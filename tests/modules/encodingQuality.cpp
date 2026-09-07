#include <vips/vips.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "EncodingQuality.hpp"
#include "MimeIDs.hpp"
#include "ModuleFile.hpp"
#include "ThumbnailInfo.hpp"
#include "ipc/Protocol.hpp"

namespace idhan::test
{

static const int VIPS_READY { VIPS_INIT( "IDHANModuleTests" ) };

//! Annex K.1 at quality 50 in DQT zigzag order.
static constexpr QuantizationTable STANDARD_LUMA_ZIGZAG {
	{ 16, 11, 12, 14, 12, 10, 16, 14,  13, 14, 18, 17, 16, 19,  24,  40,  26, 24, 22, 22, 24, 49,
	  35, 37, 29, 40, 58, 51, 61, 60,  57, 51, 56, 55, 64, 72,  92,  78,  64, 68, 87, 69, 55, 56,
	  80, 109, 81, 87, 95, 98, 103, 104, 103, 62, 77, 113, 121, 112, 100, 120, 92, 101, 103, 99 }
};

static constexpr QuantizationTable STANDARD_CHROMA_ZIGZAG {
	{ 17, 18, 18, 24, 21, 24, 47, 26, 26, 47, 99, 66, 56, 66, 99, 99, 99, 99, 99, 99, 99, 99,
	  99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
	  99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99 }
};

static void appendMarker( std::vector< std::byte >& bytes, const std::uint8_t marker )
{
	bytes.emplace_back( std::byte { 0xFF } );
	bytes.emplace_back( static_cast< std::byte >( marker ) );
}

//! Segment length includes the length field but excludes the marker.
static void appendSegment(
	std::vector< std::byte >& bytes,
	const std::uint8_t marker,
	const std::vector< std::uint8_t >& payload )
{
	appendMarker( bytes, marker );
	const auto length { payload.size() + 2 };
	bytes.emplace_back( static_cast< std::byte >( length >> 8 ) );
	bytes.emplace_back( static_cast< std::byte >( length & 0xFF ) );
	for ( const auto value : payload ) bytes.emplace_back( static_cast< std::byte >( value ) );
}

static void appendQuantizationTable(
	std::vector< std::byte >& bytes,
	const std::uint8_t id,
	const QuantizationTable& table )
{
	std::vector< std::uint8_t > payload { id };
	for ( const auto entry : table ) payload.emplace_back( static_cast< std::uint8_t >( entry ) );
	appendSegment( bytes, 0xDB, payload );
}

//! JPEG header fixture without an entropy-coded scan.
static std::vector< std::byte > jpegHeader(
	const std::uint8_t frame_marker,
	const std::uint8_t luma_sampling,
	const bool chroma,
	const std::vector< std::uint8_t >& comment = {} )
{
	std::vector< std::byte > bytes {};
	appendMarker( bytes, 0xD8 );
	if ( !comment.empty() ) appendSegment( bytes, 0xFE, comment );
	appendQuantizationTable( bytes, 0, STANDARD_LUMA_ZIGZAG );
	if ( chroma ) appendQuantizationTable( bytes, 1, STANDARD_CHROMA_ZIGZAG );

	std::vector< std::uint8_t > frame { 8, 0, 64, 0, 64, static_cast< std::uint8_t >( chroma ? 3 : 1 ) };
	frame.insert( frame.end(), { 1, luma_sampling, 0 } );
	if ( chroma ) frame.insert( frame.end(), { 2, 0x11, 1, 3, 0x11, 1 } );
	appendSegment( bytes, frame_marker, frame );

	appendMarker( bytes, 0xDA );
	return bytes;
}

static FileEncoding readEncoding( const std::vector< std::byte >& bytes, const MimeID mime_id )
{
	const auto file { ModuleFile::fromBytes( bytes ) };
	REQUIRE( file != nullptr );
	return readFileEncoding( *file, mime_id );
}

static std::optional< EncodingQuality > readQuality( const std::vector< std::byte >& bytes, const MimeID mime_id )
{
	return readEncoding( bytes, mime_id ).quality;
}

//! Use a gradient to exercise nonzero spatial frequencies during encoding.
static VipsImagePtr gradient( std::vector< std::uint8_t >& pixels )
{
	constexpr int EDGE { 64 };
	pixels.resize( EDGE * EDGE * 3 );
	for ( int y = 0; y < EDGE; ++y )
		for ( int x = 0; x < EDGE; ++x )
		{
			const auto offset { static_cast< std::size_t >( ( ( y * EDGE ) + x ) * 3 ) };
			pixels[ offset ] = static_cast< std::uint8_t >( x * 4 );
			pixels[ offset + 1 ] = static_cast< std::uint8_t >( y * 4 );
			pixels[ offset + 2 ] = static_cast< std::uint8_t >( ( x ^ y ) * 4 );
		}

	return VipsImagePtr {
		vips_image_new_from_memory( pixels.data(), pixels.size(), EDGE, EDGE, 3, VIPS_FORMAT_UCHAR )
	};
}

static std::vector< std::byte > toBytes( void* const buffer, const std::size_t length )
{
	auto* const start { static_cast< std::byte* >( buffer ) };
	std::vector< std::byte > bytes( start, start + length );
	g_free( buffer );
	return bytes;
}

TEST_CASE( "The standard quantization tables read back as quality 50", "[encoding][modules]" )
{
	CHECK( estimateJpegQuality( STANDARD_LUMA_ZIGZAG, STANDARD_CHROMA_ZIGZAG ) == 50 );
	CHECK( estimateJpegQuality( STANDARD_LUMA_ZIGZAG, std::nullopt ) == 50 );

	QuantizationTable ones {};
	ones.fill( 1 );
	CHECK( estimateJpegQuality( ones, std::nullopt ) == 100 );
}

TEST_CASE( "A JPEG header gives up its quality, subsampling and scan type", "[encoding][modules]" )
{
	const auto baseline { readQuality( jpegHeader( 0xC0, 0x22, true ), mime_ids::IMAGE_JPEG ) };
	REQUIRE( baseline );
	CHECK_FALSE( baseline->lossless );
	CHECK( baseline->quality == 50 );
	CHECK( baseline->subsampling == ChromaSubsampling::BOTH );
	CHECK( baseline->progressive == false );

	const auto full_chroma { readQuality( jpegHeader( 0xC0, 0x11, true ), mime_ids::IMAGE_JPEG ) };
	REQUIRE( full_chroma );
	CHECK( full_chroma->subsampling == ChromaSubsampling::NONE );

	const auto wide { readQuality( jpegHeader( 0xC0, 0x21, true ), mime_ids::IMAGE_JPEG ) };
	REQUIRE( wide );
	CHECK( wide->subsampling == ChromaSubsampling::HORIZONTAL );

	const auto grey { readQuality( jpegHeader( 0xC0, 0x11, false ), mime_ids::IMAGE_JPEG ) };
	REQUIRE( grey );
	CHECK( grey->subsampling == ChromaSubsampling::GREYSCALE );

	const auto progressive { readQuality( jpegHeader( 0xC2, 0x22, true ), mime_ids::IMAGE_JPEG ) };
	REQUIRE( progressive );
	CHECK( progressive->progressive == true );

	// SOF3 is lossless and has no quantization-based quality estimate.
	const auto lossless { readQuality( jpegHeader( 0xC3, 0x11, true ), mime_ids::IMAGE_JPEG ) };
	REQUIRE( lossless );
	CHECK( lossless->lossless );
	CHECK_FALSE( lossless->quality );
	// Progressive status is unavailable for lossless JPEG and unsupported for PNG.
	CHECK_FALSE( lossless->progressive );
}

static std::vector< std::uint8_t > commentBytes( const std::string_view text )
{
	return { text.begin(), text.end() };
}

TEST_CASE( "A COM segment names the encoder that wrote the file", "[encoding][modules]" )
{
	constexpr std::string_view SAI { "Paint Tool -SAI- JPEG Encoder" };

	const auto commented { readEncoding( jpegHeader( 0xC0, 0x22, true, commentBytes( SAI ) ), mime_ids::IMAGE_JPEG ) };
	REQUIRE( commented.comment );
	CHECK( *commented.comment == SAI );
	REQUIRE( commented.quality );
	CHECK( commented.quality->quality == 50 );

	// An empty JPEG comment is distinct from an unparsed comment.
	const auto bare { readEncoding( jpegHeader( 0xC0, 0x22, true ), mime_ids::IMAGE_JPEG ) };
	REQUIRE( bare.comment );
	CHECK( bare.comment->empty() );

	std::vector< std::uint8_t > pixels {};
	const VipsImagePtr image { gradient( pixels ) };
	REQUIRE( image != nullptr );
	void* buffer { nullptr };
	std::size_t length { 0 };
	REQUIRE( vips_pngsave_buffer( image.get(), &buffer, &length, nullptr ) == 0 );
	CHECK_FALSE( readEncoding( toBytes( buffer, length ), mime_ids::IMAGE_PNG ).comment );
}

TEST_CASE( "Comment text is trimmed to what a database column can hold", "[encoding][modules]" )
{
	// A NUL would be rejected outright by a Postgres text column.
	std::vector< std::uint8_t > payload { commentBytes( "  ok" ) };
	payload.insert( payload.end(), { 0x00, 0x07, 0x1B } );
	payload.insert( payload.end(), { 0xE2, 0x9C, 0x93 } ); // U+2713, which survives intact
	payload.insert( payload.end(), { 0xFF, 0xFE } ); // never valid UTF-8
	payload.insert( payload.end(), { ' ', ' ' } );

	const auto stripped { readEncoding( jpegHeader( 0xC0, 0x22, true, payload ), mime_ids::IMAGE_JPEG ) };
	REQUIRE( stripped.comment );
	CHECK( *stripped.comment == "ok\u2713" );

	const auto over_long { readEncoding(
		jpegHeader( 0xC0, 0x22, true, commentBytes( std::string( COMMENT_LIMIT * 2, 'x' ) ) ),
		mime_ids::IMAGE_JPEG ) };
	REQUIRE( over_long.comment );
	CHECK( over_long.comment->size() == COMMENT_LIMIT );
}

TEST_CASE( "Bytes that are not a JPEG answer nothing", "[encoding][modules]" )
{
	const std::vector< std::byte > junk( 512, std::byte { 0x7F } );
	CHECK_FALSE( readQuality( junk, mime_ids::IMAGE_JPEG ) );

	auto truncated { jpegHeader( 0xC0, 0x22, true ) };
	truncated.resize( 40 );
	CHECK_FALSE( readQuality( truncated, mime_ids::IMAGE_JPEG ) );
}

TEST_CASE( "A real encoder's quality comes back in the order it was asked for", "[encoding][modules]" )
{
	REQUIRE( VIPS_READY == 0 );

	std::vector< std::uint8_t > pixels {};
	const VipsImagePtr image { gradient( pixels ) };
	REQUIRE( image != nullptr );

	const auto encode = [ &image ]( const int quality, const VipsForeignSubsample subsample, const bool interlace )
	{
		void* buffer { nullptr };
		std::size_t length { 0 };
		const auto saved { vips_jpegsave_buffer(
			image.get(),
			&buffer,
			&length,
			"Q",
			quality,
			"subsample_mode",
			subsample,
			"interlace",
			interlace ? TRUE : FALSE,
			nullptr ) };
		REQUIRE( saved == 0 );
		return toBytes( buffer, length );
	};

	const auto low { readQuality( encode( 40, VIPS_FOREIGN_SUBSAMPLE_ON, false ), mime_ids::IMAGE_JPEG ) };
	const auto high { readQuality( encode( 92, VIPS_FOREIGN_SUBSAMPLE_OFF, true ), mime_ids::IMAGE_JPEG ) };

	REQUIRE( low );
	REQUIRE( high );
	REQUIRE( low->quality );
	REQUIRE( high->quality );

	// The absolute numbers depend on which libjpeg vips was built against, but the order does not.
	CHECK( *high->quality > *low->quality );
	CHECK( low->subsampling == ChromaSubsampling::BOTH );
	CHECK( high->subsampling == ChromaSubsampling::NONE );
	CHECK( low->progressive == false );
	CHECK( high->progressive == true );
	CHECK_FALSE( high->lossless );
}

TEST_CASE( "Lossless formats say so, and lossy WebP says the opposite", "[encoding][modules]" )
{
	REQUIRE( VIPS_READY == 0 );

	std::vector< std::uint8_t > pixels {};
	const VipsImagePtr image { gradient( pixels ) };
	REQUIRE( image != nullptr );

	void* buffer { nullptr };
	std::size_t length { 0 };
	REQUIRE( vips_pngsave_buffer( image.get(), &buffer, &length, nullptr ) == 0 );
	const auto png { readQuality( toBytes( buffer, length ), mime_ids::IMAGE_PNG ) };
	REQUIRE( png );
	CHECK( png->lossless );
	CHECK_FALSE( png->quality );
	CHECK_FALSE( png->progressive );

	REQUIRE( vips_webpsave_buffer( image.get(), &buffer, &length, "lossless", TRUE, nullptr ) == 0 );
	const auto webp_lossless { readQuality( toBytes( buffer, length ), mime_ids::IMAGE_WEBP ) };
	REQUIRE( webp_lossless );
	CHECK( webp_lossless->lossless );

	REQUIRE( vips_webpsave_buffer( image.get(), &buffer, &length, "Q", 70, nullptr ) == 0 );
	const auto webp_lossy { readQuality( toBytes( buffer, length ), mime_ids::IMAGE_WEBP ) };
	REQUIRE( webp_lossy );
	CHECK_FALSE( webp_lossy->lossless );
}

TEST_CASE( "Encoding settings round trip through module IPC", "[encoding][ipc]" )
{
	MetadataInfo metadata {};
	metadata.m_simple_type = SimpleMimeType::IMAGE_TYPE;
	metadata.m_metadata = MetadataInfoImage {
		.width = 64,
		.height = 64,
		.channels = 3,
		.phash = std::nullopt,
		.embedded = {},
		.encoding = EncodingQuality {
			.lossless = false,
			.quality = 82,
			.subsampling = ChromaSubsampling::HORIZONTAL,
			.progressive = true }
	};

	const auto decoded { ipc::metadataInfoFromJson( ipc::toJson( metadata ) ) };
	REQUIRE( decoded );
	const auto& encoding { std::get< MetadataInfoImage >( decoded->m_metadata ).encoding };
	REQUIRE( encoding );
	CHECK_FALSE( encoding->lossless );
	CHECK( encoding->quality == 82 );
	CHECK( encoding->subsampling == ChromaSubsampling::HORIZONTAL );
	CHECK( encoding->progressive == true );

	// A module built before the field existed sends no object at all.
	auto absent { ipc::toJson( metadata ) };
	absent[ ipc::field::VALUE ].removeMember( "encoding" );
	const auto older { ipc::metadataInfoFromJson( absent ) };
	REQUIRE( older );
	CHECK_FALSE( std::get< MetadataInfoImage >( older->m_metadata ).encoding );
}

} // namespace idhan::test
