#include "PerceptualHash.hpp"

#include <vips/vips.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <numbers>
#include <span>
#include <vector>

#include "MimeIDs.hpp"
#include "ThumbnailInfo.hpp"

namespace idhan
{
namespace
{

constexpr std::size_t DCT_SIZE { 32 };
constexpr std::size_t HASH_SIZE { 8 };
constexpr PerceptualHash BLANK_HASH {
	{ std::byte { 0x80 },
	  std::byte { 0 },
	  std::byte { 0 },
	  std::byte { 0 },
	  std::byte { 0 },
	  std::byte { 0 },
	  std::byte { 0 },
	  std::byte { 0 } }
};

struct Pixels
{
	std::vector< std::uint8_t > bytes {};
	std::size_t width { 0 };
	std::size_t height { 0 };
	std::size_t bands { 0 };
};

[[nodiscard]] std::uint8_t roundedByte( const double value )
{
	return static_cast< std::uint8_t >( std::clamp( std::nearbyint( value ), 0.0, 255.0 ) );
}

[[nodiscard]] std::vector< std::uint8_t > areaReduce(
	const std::span< const std::uint8_t > source,
	const std::size_t source_width,
	const std::size_t source_height,
	const std::size_t bands,
	const std::size_t target_width,
	const std::size_t target_height )
{
	std::vector< std::uint8_t > target( target_width * target_height * bands );
	const double scale_x { static_cast< double >( source_width ) / static_cast< double >( target_width ) };
	const double scale_y { static_cast< double >( source_height ) / static_cast< double >( target_height ) };

	for ( std::size_t target_y = 0; target_y < target_height; ++target_y )
	{
		const double source_y_begin { static_cast< double >( target_y ) * scale_y };
		const double source_y_end { static_cast< double >( target_y + 1 ) * scale_y };
		const auto first_y { static_cast< std::size_t >( std::floor( source_y_begin ) ) };
		const auto last_y { std::min( source_height, static_cast< std::size_t >( std::ceil( source_y_end ) ) ) };

		for ( std::size_t target_x = 0; target_x < target_width; ++target_x )
		{
			const double source_x_begin { static_cast< double >( target_x ) * scale_x };
			const double source_x_end { static_cast< double >( target_x + 1 ) * scale_x };
			const auto first_x { static_cast< std::size_t >( std::floor( source_x_begin ) ) };
			const auto last_x { std::min( source_width, static_cast< std::size_t >( std::ceil( source_x_end ) ) ) };

			for ( std::size_t band = 0; band < bands; ++band )
			{
				double sum { 0.0 };
				double weight_sum { 0.0 };

				for ( std::size_t source_y = first_y; source_y < last_y; ++source_y )
				{
					const double y_weight { std::max(
						0.0,
						std::min( source_y_end, static_cast< double >( source_y + 1 ) )
							- std::max( source_y_begin, static_cast< double >( source_y ) ) ) };

					for ( std::size_t source_x = first_x; source_x < last_x; ++source_x )
					{
						const double x_weight { std::max(
							0.0,
							std::min( source_x_end, static_cast< double >( source_x + 1 ) )
								- std::max( source_x_begin, static_cast< double >( source_x ) ) ) };
						const double weight { x_weight * y_weight };
						const auto offset { ( source_y * source_width + source_x ) * bands + band };
						sum += static_cast< double >( source[ offset ] ) * weight;
						weight_sum += weight;
					}
				}

				target[ ( target_y * target_width + target_x ) * bands + band ] = roundedByte( sum / weight_sum );
			}
		}
	}

	return target;
}

[[nodiscard]] std::vector< std::uint8_t > toGrayscale( Pixels pixels )
{
	std::vector< std::uint8_t > grayscale( pixels.width * pixels.height );
	const bool has_alpha { pixels.bands == 2 || pixels.bands == 4 };

	for ( std::size_t pixel = 0; pixel < grayscale.size(); ++pixel )
	{
		const auto offset { pixel * pixels.bands };
		std::uint8_t gray { pixels.bytes[ offset ] };

		if ( pixels.bands >= 3 )
		{
			const std::uint32_t red { pixels.bytes[ offset ] };
			const std::uint32_t green { pixels.bytes[ offset + 1 ] };
			const std::uint32_t blue { pixels.bytes[ offset + 2 ] };
			gray = static_cast< std::uint8_t >( ( red * 4899 + green * 9617 + blue * 1868 + 8192 ) >> 14 );
		}

		if ( has_alpha )
		{
			const std::uint32_t alpha { pixels.bytes[ offset + pixels.bands - 1 ] };
			gray =
				static_cast< std::uint8_t >( ( static_cast< std::uint32_t >( gray ) * alpha ) / 255 + ( 255 - alpha ) );
		}

		grayscale[ pixel ] = gray;
	}

	return grayscale;
}

[[nodiscard]] std::array< std::array< double, DCT_SIZE >, DCT_SIZE > dctWeights()
{
	std::array< std::array< double, DCT_SIZE >, DCT_SIZE > weights {};
	for ( std::size_t frequency = 0; frequency < DCT_SIZE; ++frequency )
	{
		const double scale { frequency == 0 ? std::sqrt( 1.0 / DCT_SIZE ) : std::sqrt( 2.0 / DCT_SIZE ) };
		for ( std::size_t position = 0; position < DCT_SIZE; ++position )
		{
			weights[ frequency ][ position ] =
				scale
				* std::cos(
					std::numbers::pi * ( static_cast< double >( position ) + 0.5 ) * static_cast< double >( frequency )
					/ DCT_SIZE );
		}
	}
	return weights;
}

[[nodiscard]] PerceptualHash hashGrayscale( const std::span< const std::uint8_t > grayscale )
{
	static const auto weights { dctWeights() };
	std::array< std::array< double, DCT_SIZE >, DCT_SIZE > horizontal {};

	for ( std::size_t y = 0; y < DCT_SIZE; ++y )
		for ( std::size_t frequency_x = 0; frequency_x < DCT_SIZE; ++frequency_x )
			for ( std::size_t x = 0; x < DCT_SIZE; ++x )
				horizontal[ y ][ frequency_x ] += static_cast< double >( grayscale[ y * DCT_SIZE + x ] )
				                                * weights[ frequency_x ][ x ];

	std::array< double, HASH_SIZE * HASH_SIZE > useful {};
	for ( std::size_t frequency_y = 0; frequency_y < HASH_SIZE; ++frequency_y )
		for ( std::size_t frequency_x = 0; frequency_x < HASH_SIZE; ++frequency_x )
		{
			for ( std::size_t y = 0; y < DCT_SIZE; ++y )
				useful[ frequency_y * HASH_SIZE + frequency_x ] +=
					horizontal[ y ][ frequency_x ] * weights[ frequency_y ][ y ];

			// FFT-based orthonormal DCTs return exact zero for a flat signal. Direct summation leaves
			// machine-epsilon cancellation noise, which would otherwise turn a blank into random bits.
			auto& coefficient { useful[ frequency_y * HASH_SIZE + frequency_x ] };
			if ( std::abs( coefficient ) < 1e-9 ) coefficient = 0.0;
		}

	std::array< double, HASH_SIZE * HASH_SIZE - 1 > median_values {};
	std::copy( useful.begin() + 1, useful.end(), median_values.begin() );
	std::ranges::sort( median_values );
	const double median { median_values[ median_values.size() / 2 ] };

	PerceptualHash hash {};
	for ( std::size_t y = 0; y < HASH_SIZE; ++y )
	{
		std::uint32_t byte { 0 };
		for ( std::size_t x = 0; x < HASH_SIZE; ++x )
		{
			byte <<= 1;
			if ( useful[ y * HASH_SIZE + x ] > median ) byte |= 1;
		}
		hash[ y ] = static_cast< std::byte >( static_cast< std::uint8_t >( byte ) );
	}
	return hash;
}

[[nodiscard]] int blankDistance( const PerceptualHash& hash )
{
	return perceptualHashDistance( hash, BLANK_HASH );
}

[[nodiscard]] std::expected< Pixels, ModuleError > materialize( VipsImage* image )
{
	VipsImage* oriented_raw { nullptr };
	if ( vips_autorot( image, &oriented_raw, nullptr ) != 0 ) return std::unexpected( "Failed to orient image" );
	VipsImagePtr normalized { oriented_raw };

	const auto bands { vips_image_get_bands( normalized.get() ) };
	if ( bands < 1 || bands > 4 ) return std::unexpected( "Unsupported image band layout" );

	const auto interpretation { vips_image_get_interpretation( normalized.get() ) };
	if ( bands >= 3 && interpretation != VIPS_INTERPRETATION_sRGB )
	{
		VipsImage* srgb_raw { nullptr };
		if ( vips_colourspace( normalized.get(), &srgb_raw, VIPS_INTERPRETATION_sRGB, nullptr ) != 0 )
			return std::unexpected( "Failed to convert image to sRGB" );
		normalized.reset( srgb_raw );
	}

	if ( vips_image_get_format( normalized.get() ) != VIPS_FORMAT_UCHAR )
	{
		VipsImage* byte_raw { nullptr };
		const auto format { vips_image_get_format( normalized.get() ) };
		if ( format == VIPS_FORMAT_USHORT )
		{
			const double multiplier[] { 1.0 / 256.0 };
			const double offset[] { 0.0 };
			VipsImage* scaled_raw { nullptr };
			if ( vips_linear( normalized.get(), &scaled_raw, multiplier, offset, 1, nullptr ) != 0 )
				return std::unexpected( "Failed to normalize 16-bit image" );
			normalized.reset( scaled_raw );
		}

		if ( vips_cast( normalized.get(), &byte_raw, VIPS_FORMAT_UCHAR, nullptr ) != 0 )
			return std::unexpected( "Failed to normalize image to 8-bit" );
		normalized.reset( byte_raw );
	}

	std::size_t byte_count { 0 };
	std::unique_ptr< void, decltype( &g_free ) > memory {
		vips_image_write_to_memory( normalized.get(), &byte_count ), &g_free
	};
	if ( memory == nullptr ) return std::unexpected( "Failed to materialize image pixels" );

	Pixels pixels {};
	pixels.width = static_cast< std::size_t >( vips_image_get_width( normalized.get() ) );
	pixels.height = static_cast< std::size_t >( vips_image_get_height( normalized.get() ) );
	pixels.bands = static_cast< std::size_t >( vips_image_get_bands( normalized.get() ) );
	if ( pixels.width == 0 || pixels.height == 0 || byte_count != pixels.width * pixels.height * pixels.bands )
		return std::unexpected( "Materialized image has an invalid layout" );

	pixels.bytes.resize( byte_count );
	std::memcpy( pixels.bytes.data(), memory.get(), byte_count );
	return pixels;
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

	auto materialized { materialize( image ) };
	if ( !materialized ) return std::unexpected( materialized.error() );

	if ( materialized->bands == 2 || materialized->bands == 4 )
	{
		const auto target_width { std::min< std::size_t >( 256, materialized->width ) };
		const auto target_height { std::min< std::size_t >( 256, materialized->height ) };
		if ( target_width != materialized->width || target_height != materialized->height )
		{
			materialized->bytes = areaReduce(
				materialized->bytes,
				materialized->width,
				materialized->height,
				materialized->bands,
				target_width,
				target_height );
			materialized->width = target_width;
			materialized->height = target_height;
		}
	}

	const auto grayscale_width { materialized->width };
	const auto grayscale_height { materialized->height };
	const auto grayscale { toGrayscale( std::move( *materialized ) ) };
	const auto tiny { areaReduce( grayscale, grayscale_width, grayscale_height, 1, DCT_SIZE, DCT_SIZE ) };
	const auto hash { hashGrayscale( tiny ) };
	if ( blankDistance( hash ) <= 4 ) return std::optional< PerceptualHash > {};
	return std::optional< PerceptualHash > { hash };
}

} // namespace idhan
