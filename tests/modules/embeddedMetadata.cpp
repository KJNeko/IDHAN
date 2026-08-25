#include <vips/vips.h>

#include <array>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "EmbeddedMetadata.hpp"
#include "ThumbnailInfo.hpp"
#include "ipc/Protocol.hpp"

namespace idhan::test
{

[[nodiscard]] static VipsImagePtr blankImage( std::vector< std::uint8_t >& pixels )
{
	pixels.assign( 4 * 4 * 3, 0 );
	return VipsImagePtr { vips_image_new_from_memory( pixels.data(), pixels.size(), 4, 4, 3, VIPS_FORMAT_UCHAR ) };
}

static void setBlob( VipsImage* const image, const char* const name )
{
	constexpr std::array< std::uint8_t, 4 > payload { { 1, 2, 3, 4 } };
	vips_image_set_blob_copy( image, name, payload.data(), payload.size() );
}

TEST_CASE( "Embedded metadata detection reads the blocks a decoder attached", "[metadata][modules]" )
{
	std::vector< std::uint8_t > pixels {};
	VipsImagePtr image { blankImage( pixels ) };
	REQUIRE( image != nullptr );

	const auto bare { detectEmbeddedMetadata( image.get() ) };
	CHECK_FALSE( bare.exif );
	CHECK_FALSE( bare.gps );
	CHECK_FALSE( bare.xmp );
	CHECK_FALSE( bare.iptc );
	CHECK_FALSE( bare.icc_profile );

	setBlob( image.get(), VIPS_META_EXIF_NAME );
	setBlob( image.get(), VIPS_META_ICC_NAME );

	const auto with_exif { detectEmbeddedMetadata( image.get() ) };
	CHECK( with_exif.exif );
	CHECK( with_exif.icc_profile );
	CHECK_FALSE( with_exif.gps );
	CHECK_FALSE( with_exif.xmp );

	// Every decoded EXIF tag becomes its own field; IFD 3 is the GPS directory.
	vips_image_set_string( image.get(), "exif-ifd3-GPSLatitude", "51, 30, 0 (51, 30, 0, Rational, 3 components)" );
	CHECK( detectEmbeddedMetadata( image.get() ).gps );

	setBlob( image.get(), VIPS_META_XMP_NAME );
	setBlob( image.get(), VIPS_META_IPTC_NAME );

	const auto full { detectEmbeddedMetadata( image.get() ) };
	CHECK( full.xmp );
	CHECK( full.iptc );
}

TEST_CASE( "A GPS directory alone is not reported without an EXIF block", "[metadata][modules]" )
{
	std::vector< std::uint8_t > pixels {};
	VipsImagePtr image { blankImage( pixels ) };
	REQUIRE( image != nullptr );

	vips_image_set_string( image.get(), "exif-ifd3-GPSLatitude", "51, 30, 0" );
	CHECK_FALSE( detectEmbeddedMetadata( image.get() ).gps );
}

TEST_CASE( "Embedded metadata flags round trip through module IPC", "[metadata][ipc]" )
{
	MetadataInfo metadata {};
	metadata.m_simple_type = SimpleMimeType::IMAGE_TYPE;
	metadata.m_metadata = MetadataInfoImage {
		.width = 64,
		.height = 32,
		.channels = 3,
		.phash = std::nullopt,
		.embedded = EmbeddedMetadata { .exif = true, .gps = true, .xmp = false, .iptc = false, .icc_profile = true }
	};

	const auto json { ipc::toJson( metadata ) };
	CHECK( json[ ipc::field::VALUE ][ "embedded" ][ "exif" ].asBool() );

	const auto decoded { ipc::metadataInfoFromJson( json ) };
	REQUIRE( decoded );
	const auto& embedded { std::get< MetadataInfoImage >( decoded->m_metadata ).embedded };
	CHECK( embedded.exif );
	CHECK( embedded.gps );
	CHECK_FALSE( embedded.xmp );
	CHECK_FALSE( embedded.iptc );
	CHECK( embedded.icc_profile );

	// A module built before the flags existed sends no object at all.
	auto absent_json { json };
	absent_json[ ipc::field::VALUE ].removeMember( "embedded" );
	const auto absent { ipc::metadataInfoFromJson( absent_json ) };
	REQUIRE( absent );
	CHECK_FALSE( std::get< MetadataInfoImage >( absent->m_metadata ).embedded.exif );
}

} // namespace idhan::test
