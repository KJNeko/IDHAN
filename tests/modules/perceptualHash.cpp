#include <vips/vips.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "MimeIDs.hpp"
#include "PerceptualHash.hpp"
#include "ThumbnailInfo.hpp"
#include "ipc/Protocol.hpp"

namespace idhan::test
{
namespace
{

const int VIPS_STATUS { VIPS_INIT( "IDHANModuleTests" ) };

[[nodiscard]] std::string hex( const PerceptualHash& hash )
{
	constexpr char HEX[] { "0123456789abcdef" };
	std::string result( hash.size() * 2, '0' );
	for ( std::size_t i = 0; i < hash.size(); ++i )
	{
		const auto byte { static_cast< std::uint8_t >( hash[ i ] ) };
		result[ i * 2 ] = HEX[ byte >> 4 ];
		result[ i * 2 + 1 ] = HEX[ byte & 0x0f ];
	}
	return result;
}

[[nodiscard]] VipsImagePtr memoryImage(
	std::vector< std::uint8_t >& pixels,
	const int width,
	const int height,
	const int bands )
{
	return VipsImagePtr { vips_image_new_from_memory(
		pixels.data(), pixels.size(), width, height, bands, VIPS_FORMAT_UCHAR ) };
}

} // namespace

TEST_CASE( "Hydrus perceptual hash golden image", "[phash][modules]" )
{
	REQUIRE( VIPS_STATUS == 0 );
	VipsImagePtr image { vips_image_new_from_file( IDHAN_SOURCE_DIR "/3rd-party/hydrus/static/hydrus.png", nullptr ) };
	REQUIRE( image != nullptr );

	const auto generated { generatePerceptualHash( image.get() ) };
	REQUIRE( generated );
	REQUIRE( *generated );
	CHECK( hex( **generated ) == "b44dc7b24dcb381c" );
}

TEST_CASE( "Perceptual hash accepts supported pixel layouts and filters blanks", "[phash][modules]" )
{
	REQUIRE( VIPS_STATUS == 0 );
	for ( const int bands : { 1, 2, 3, 4 } )
	{
		CAPTURE( bands );
		std::vector< std::uint8_t > white( static_cast< std::size_t >( 48 * 40 * bands ), 255 );
		auto image { memoryImage( white, 48, 40, bands ) };
		REQUIRE( image != nullptr );

		const auto generated { generatePerceptualHash( image.get() ) };
		REQUIRE( generated );
		CHECK_FALSE( *generated );
	}
}

TEST_CASE( "Perceptual hash matches Hydrus when enlarging a small image", "[phash][modules]" )
{
	REQUIRE( VIPS_STATUS == 0 );
	std::vector< std::uint8_t > pixels( 8 * 8 * 3 );
	for ( int y = 0; y < 8; ++y )
		for ( int x = 0; x < 8; ++x )
		{
			const auto offset { static_cast< std::size_t >( ( y * 8 + x ) * 3 ) };
			pixels[ offset ] = static_cast< std::uint8_t >( ( x * 31 + y * 7 ) % 256 );
			pixels[ offset + 1 ] = static_cast< std::uint8_t >( ( x * 11 + y * 43 ) % 256 );
			pixels[ offset + 2 ] = static_cast< std::uint8_t >( ( x * 53 + y * 3 ) % 256 );
		}

	auto image { memoryImage( pixels, 8, 8, 3 ) };
	REQUIRE( image != nullptr );
	const auto generated { generatePerceptualHash( image.get() ) };
	REQUIRE( generated );
	REQUIRE( *generated );
	CHECK( hex( **generated ) == "8d2b156e9b11ee91" );
}

TEST_CASE( "Perceptual hash matches Hydrus alpha compositing", "[phash][modules]" )
{
	REQUIRE( VIPS_STATUS == 0 );
	constexpr int WIDTH { 300 };
	constexpr int HEIGHT { 270 };
	std::vector< std::uint8_t > pixels( WIDTH * HEIGHT * 4 );
	for ( int y = 0; y < HEIGHT; ++y )
		for ( int x = 0; x < WIDTH; ++x )
		{
			const auto offset { static_cast< std::size_t >( ( y * WIDTH + x ) * 4 ) };
			pixels[ offset ] = static_cast< std::uint8_t >( ( x * 31 + y * 7 ) % 256 );
			pixels[ offset + 1 ] = static_cast< std::uint8_t >( ( x * 11 + y * 43 ) % 256 );
			pixels[ offset + 2 ] = static_cast< std::uint8_t >( ( x * 53 + y * 3 ) % 256 );
			pixels[ offset + 3 ] = static_cast< std::uint8_t >( ( x * 17 + y * 19 ) % 256 );
		}

	auto image { memoryImage( pixels, WIDTH, HEIGHT, 4 ) };
	REQUIRE( image != nullptr );
	const auto generated { generatePerceptualHash( image.get() ) };
	REQUIRE( generated );
	REQUIRE( *generated );
	CHECK( hex( **generated ) == "b51d256f201d2de5" );
}

TEST_CASE( "Perceptual hash MIME eligibility is static-image only", "[phash][modules]" )
{
	CHECK( isPerceptualHashMime( mime_ids::IMAGE_JPEG ) );
	CHECK( isPerceptualHashMime( mime_ids::IMAGE_PNG ) );
	CHECK( isPerceptualHashMime( mime_ids::IMAGE_WEBP ) );
	CHECK( isPerceptualHashMime( mime_ids::IMAGE_AVIF ) );
	CHECK( isPerceptualHashMime( mime_ids::IMAGE_TIFF ) );
	CHECK_FALSE( isPerceptualHashMime( mime_ids::ANIMATION_GIF ) );
	CHECK_FALSE( isPerceptualHashMime( mime_ids::ANIMATION_APNG ) );
}

TEST_CASE( "Perceptual hash distance counts differing bits", "[phash][distance][modules]" )
{
	const PerceptualHash zero {};
	PerceptualHash one_bit {};
	one_bit[ 7 ] = std::byte { 0x01 };
	PerceptualHash five_bits {};
	five_bits[ 0 ] = std::byte { 0xf0 };
	five_bits[ 7 ] = std::byte { 0x01 };
	PerceptualHash all_bits {};
	all_bits.fill( std::byte { 0xff } );

	CHECK( perceptualHashDistance( zero, zero ) == 0 );
	CHECK( perceptualHashDistance( zero, one_bit ) == 1 );
	CHECK( perceptualHashDistance( one_bit, five_bits ) == 4 );
	CHECK( perceptualHashDistance( zero, five_bits ) == 5 );
	CHECK( perceptualHashDistance( zero, all_bits ) == 64 );
	CHECK( perceptualHashDistance( five_bits, zero ) == perceptualHashDistance( zero, five_bits ) );
}

TEST_CASE( "Perceptual hashes round trip through module IPC", "[phash][ipc]" )
{
	MetadataInfo metadata {};
	metadata.m_simple_type = SimpleMimeType::IMAGE_TYPE;
	metadata.m_metadata = MetadataInfoImage {
		.width = 64,
		.height = 32,
		.channels = 4,
		.phash = PerceptualHash { {
			std::byte { 0xb4 }, std::byte { 0x4d }, std::byte { 0xc7 }, std::byte { 0xb2 },
			std::byte { 0x4d }, std::byte { 0xcb }, std::byte { 0x38 }, std::byte { 0x1c }
		} }
	};

	const auto json { ipc::toJson( metadata ) };
	CHECK( json[ ipc::field::VALUE ][ "phash" ].asString() == "b44dc7b24dcb381c" );

	const auto decoded { ipc::metadataInfoFromJson( json ) };
	REQUIRE( decoded );
	const auto& image { std::get< MetadataInfoImage >( decoded->m_metadata ) };
	REQUIRE( image.phash );
	CHECK( image.phash == std::get< MetadataInfoImage >( metadata.m_metadata ).phash );

	auto absent_json { json };
	absent_json[ ipc::field::VALUE ].removeMember( "phash" );
	const auto absent { ipc::metadataInfoFromJson( absent_json ) };
	REQUIRE( absent );
	CHECK_FALSE( std::get< MetadataInfoImage >( absent->m_metadata ).phash );
}

TEST_CASE( "Module IPC rejects malformed perceptual hashes", "[phash][ipc]" )
{
	MetadataInfo metadata {};
	metadata.m_simple_type = SimpleMimeType::IMAGE_TYPE;
	metadata.m_metadata = MetadataInfoImage { .width = 1, .height = 1, .channels = 3 };
	auto json { ipc::toJson( metadata ) };

	json[ ipc::field::VALUE ][ "phash" ] = 7;
	CHECK_FALSE( ipc::metadataInfoFromJson( json ) );
	json[ ipc::field::VALUE ][ "phash" ] = "abcd";
	CHECK_FALSE( ipc::metadataInfoFromJson( json ) );
	json[ ipc::field::VALUE ][ "phash" ] = "b44dc7b24dcb381z";
	CHECK_FALSE( ipc::metadataInfoFromJson( json ) );
}

} // namespace idhan::test
