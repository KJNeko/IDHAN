#include "PerceptualHash.hpp"

#include <vips/vips.h>

#include <bit>
#include <cstdint>
#include <memory>

#include "MimeIDs.hpp"
#include "ThumbnailInfo.hpp"

namespace idhan
{
namespace
{

using ImageResult = std::expected< VipsImagePtr, ModuleError >;

[[nodiscard]] ImageResult orientImage( VipsImage* image )
{
	VipsImage* oriented_raw { nullptr };
	if ( vips_autorot( image, &oriented_raw, nullptr ) != 0 ) return std::unexpected( "Failed to orient image" );
	return VipsImagePtr { oriented_raw };
}

[[nodiscard]] ImageResult flattenAlpha( VipsImagePtr image )
{
	const auto bands { vips_image_get_bands( image.get() ) };
	const bool has_alpha { vips_image_hasalpha( image.get() ) || bands == 2 || bands == 4 };
	if ( !has_alpha ) return VipsImagePtr { image.release() };

	const double white { 255.0 };
	std::unique_ptr< VipsArrayDouble, decltype( &VipsArrayDouble_unref ) > background {
		vips_array_double_new( &white, 1 ), &VipsArrayDouble_unref
	};
	if ( background == nullptr ) return std::unexpected( "Failed to create alpha background" );

	VipsImage* flattened_raw { nullptr };
	if ( vips_flatten( image.get(), &flattened_raw, "background", background.get(), nullptr ) != 0 )
		return std::unexpected( "Failed to flatten image alpha" );
	return VipsImagePtr { flattened_raw };
}

[[nodiscard]] ImageResult convertToGrayscale( VipsImagePtr image )
{
	if ( vips_image_get_bands( image.get() ) == 1 ) return VipsImagePtr { image.release() };

	if ( vips_image_get_interpretation( image.get() ) == VIPS_INTERPRETATION_MULTIBAND )
	{
		VipsImage* rgb_raw { nullptr };
		if ( vips_copy( image.get(), &rgb_raw, "interpretation", VIPS_INTERPRETATION_sRGB, nullptr ) != 0 )
			return std::unexpected( "Failed to identify image colors" );
		image.reset( rgb_raw );
	}

	VipsImage* grayscale_raw { nullptr };
	if ( vips_colourspace( image.get(), &grayscale_raw, VIPS_INTERPRETATION_B_W, nullptr ) != 0 )
		return std::unexpected( "Failed to convert image to grayscale" );
	return VipsImagePtr { grayscale_raw };
}

[[nodiscard]] ImageResult resizeGrayscale( VipsImage* image )
{
	VipsImage* resized_raw { nullptr };
	if ( vips_thumbnail_image( image, &resized_raw, 8, "height", 8, "size", VIPS_SIZE_FORCE, nullptr ) != 0 )
		return std::unexpected( "Failed to resize grayscale image" );
	return VipsImagePtr { resized_raw };
}

[[nodiscard]] std::expected< std::optional< PerceptualHash >, ModuleError > calculateAverageHash( VipsImagePtr image )
{
	if ( vips_image_get_format( image.get() ) != VIPS_FORMAT_UCHAR )
	{
		VipsImage* byte_raw { nullptr };
		if ( vips_cast( image.get(), &byte_raw, VIPS_FORMAT_UCHAR, nullptr ) != 0 )
			return std::unexpected( "Failed to convert grayscale image to bytes" );
		image.reset( byte_raw );
	}

	std::size_t byte_count { 0 };
	std::unique_ptr< void, decltype( &g_free ) > memory {
		vips_image_write_to_memory( image.get(), &byte_count ), &g_free
	};
	if ( memory == nullptr ) return std::unexpected( "Failed to read grayscale pixels" );
	if ( byte_count != 64 ) return std::unexpected( "Expected an 8x8 grayscale image" );

	const auto* pixels { static_cast< const std::uint8_t* >( memory.get() ) };
	std::uint64_t sum { 0 };
	for ( std::size_t i = 0; i < byte_count; ++i ) sum += pixels[ i ];
	const auto average { static_cast< std::uint8_t >( sum / byte_count ) };

	PerceptualHash hash {};
	for ( std::size_t i = 0; i < byte_count; ++i )
	{
		auto& byte { hash[ i / 8 ] };
		byte <<= 1;
		if ( pixels[ i ] > average ) byte |= std::byte { 1 };
	}

	for ( const auto byte : hash )
		if ( byte != std::byte { 0 } ) return std::optional< PerceptualHash > { hash };

	return std::optional< PerceptualHash > {};
}

} // namespace

bool isPerceptualHashMime( const MimeID mime_id )
{
	return mime_id == mime_ids::IMAGE_JPEG || mime_id == mime_ids::IMAGE_PNG || mime_id == mime_ids::IMAGE_WEBP
	    || mime_id == mime_ids::IMAGE_AVIF || mime_id == mime_ids::IMAGE_TIFF;
}

std::uint8_t perceptualHashDistance( const PerceptualHash& left, const PerceptualHash& right )
{
	unsigned int distance { 0 };
	for ( std::size_t i = 0; i < left.size(); ++i )
	{
		const auto difference { static_cast< unsigned int >( static_cast< std::uint8_t >( left[ i ] ) )
			                    ^ static_cast< unsigned int >( static_cast< std::uint8_t >( right[ i ] ) ) };
		distance += static_cast< unsigned int >( std::popcount( difference ) );
	}
	return static_cast< std::uint8_t >( distance );
}

std::expected< std::optional< PerceptualHash >, ModuleError > generatePerceptualHash( VipsImage* image )
{
	if ( image == nullptr ) return std::unexpected( "Cannot hash a null image" );

	auto oriented { orientImage( image ) };
	if ( !oriented ) return std::unexpected( oriented.error() );

	auto flattened { flattenAlpha( std::move( *oriented ) ) };
	if ( !flattened ) return std::unexpected( flattened.error() );

	auto grayscale { convertToGrayscale( std::move( *flattened ) ) };
	if ( !grayscale ) return std::unexpected( grayscale.error() );

	auto resized { resizeGrayscale( grayscale->get() ) };
	if ( !resized ) return std::unexpected( resized.error() );

	return calculateAverageHash( std::move( *resized ) );
}

} // namespace idhan
