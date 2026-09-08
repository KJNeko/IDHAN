#include "PerceptualHash.hpp"

#include <vips/vips.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <vector>

#include "MimeIDs.hpp"
#include "ThumbnailInfo.hpp"

namespace idhan
{

static constexpr int PHASH_DIMENSION { 8 };
static constexpr int TINY_DIMENSION { PHASH_DIMENSION * 4 };
static constexpr int ALPHA_PRESCALE_MAX { 256 };

//! cv::COLOR_RGB2GRAY's fixed-point Rec.601 luma weights for CV_8U; they sum to exactly 1 << LUMA_SHIFT.
static constexpr int LUMA_SHIFT { 15 };
static constexpr int LUMA_R { 9798 };
static constexpr int LUMA_G { 19235 };
static constexpr int LUMA_B { 3735 };

//! Fixed-point precision matching OpenCV INTER_RESIZE_COEF_BITS.
static constexpr int LINEAR_COEF_BITS { 11 };
static constexpr int LINEAR_COEF_SCALE { 1 << LINEAR_COEF_BITS };

static constexpr PerceptualHash BLANK_HASH {
	{ std::byte { 0x80 },
	  std::byte { 0x00 },
	  std::byte { 0x00 },
	  std::byte { 0x00 },
	  std::byte { 0x00 },
	  std::byte { 0x00 },
	  std::byte { 0x00 },
	  std::byte { 0x00 } }
};
static constexpr std::uint8_t BLANK_MAX_DISTANCE { 4 };

//! Hydrus translucency threshold for alpha-channel detection.
static constexpr int ALPHA_OPAQUE_FLOOR { 251 };
//! Hydrus visibility threshold for alpha-channel detection.
static constexpr int ALPHA_TRANSPARENT_CEILING { 4 };

struct Plane
{
	std::vector< std::uint8_t > pixels {};
	int width { 0 };
	int height { 0 };
	int channels { 1 };

	[[nodiscard]] std::size_t stride() const
	{
		return static_cast< std::size_t >( width ) * static_cast< std::size_t >( channels );
	}

	[[nodiscard]] const std::uint8_t* row( const int y ) const
	{
		return pixels.data() + ( static_cast< std::size_t >( y ) * stride() );
	}
};

struct AreaWeight
{
	int di;
	int si;
	float alpha;
};

struct LinearWeight
{
	int si;
	int a0;
	int a1;
};

static std::uint8_t saturateByte( const float value )
{
	return static_cast< std::uint8_t >( std::clamp< long >( std::lrintf( value ), 0, 255 ) );
}

static std::uint8_t luma( const int r, const int g, const int b )
{
	const int weighted { ( r * LUMA_R ) + ( g * LUMA_G ) + ( b * LUMA_B ) + ( 1 << ( LUMA_SHIFT - 1 ) ) };
	return static_cast< std::uint8_t >( weighted >> LUMA_SHIFT );
}

//! Match OpenCV computeResizeAreaTab area-overlap weights.
static std::vector< AreaWeight > areaWeights( const int ssize, const int dsize, const int cn, const double scale )
{
	std::vector< AreaWeight > tab {};
	for ( int dx = 0; dx < dsize; ++dx )
	{
		const double fsx1 { dx * scale };
		const double fsx2 { fsx1 + scale };
		const double cell { std::min( scale, ssize - fsx1 ) };

		const int sx2 { std::min( static_cast< int >( std::floor( fsx2 ) ), ssize - 1 ) };
		const int sx1 { std::min( static_cast< int >( std::ceil( fsx1 ) ), sx2 ) };

		if ( sx1 - fsx1 > 1e-3 )
			tab.push_back( { dx * cn, ( sx1 - 1 ) * cn, static_cast< float >( ( sx1 - fsx1 ) / cell ) } );

		for ( int sx = sx1; sx < sx2; ++sx ) tab.push_back( { dx * cn, sx * cn, static_cast< float >( 1.0 / cell ) } );

		if ( fsx2 - sx2 > 1e-3 )
			tab.push_back(
				{ dx * cn, sx2 * cn, static_cast< float >( std::min( std::min( fsx2 - sx2, 1.0 ), cell ) / cell ) } );
	}
	return tab;
}

//! Match OpenCV INTER_AREA coefficient setup for an enlarged axis.
static std::vector< LinearWeight > linearWeights( const int ssize, const int dsize )
{
	const double inv_scale { static_cast< double >( dsize ) / ssize };
	const double scale { 1.0 / inv_scale };

	std::vector< LinearWeight > tab( static_cast< std::size_t >( dsize ) );
	for ( int dx = 0; dx < dsize; ++dx )
	{
		int sx { static_cast< int >( std::floor( dx * scale ) ) };
		auto fx { static_cast< float >( ( dx + 1 ) - ( ( sx + 1 ) * inv_scale ) ) };
		fx = fx <= 0.0f ? 0.0f : fx - std::floor( fx );

		if ( sx < 0 )
		{
			fx = 0.0f;
			sx = 0;
		}
		if ( sx >= ssize - 1 )
		{
			fx = 0.0f;
			sx = ssize - 1;
		}

		const auto a1 { static_cast< int >( std::lrintf( fx * LINEAR_COEF_SCALE ) ) };
		const auto a0 { static_cast< int >( std::lrintf( ( 1.0f - fx ) * LINEAR_COEF_SCALE ) ) };
		tab[ static_cast< std::size_t >( dx ) ] = { sx, a0, a1 };
	}
	return tab;
}

static void horizontalLinear(
	const Plane& src,
	const int sy,
	const std::vector< LinearWeight >& xtab,
	std::vector< int >& out )
{
	const auto* const row { src.row( sy ) };
	const auto channels { static_cast< std::size_t >( src.channels ) };

	for ( std::size_t dx = 0; dx < xtab.size(); ++dx )
	{
		const auto& weight { xtab[ dx ] };
		const auto left { static_cast< std::size_t >( weight.si ) * channels };
		const auto right { static_cast< std::size_t >( std::min( weight.si + 1, src.width - 1 ) ) * channels };
		for ( std::size_t c = 0; c < channels; ++c )
			out[ ( dx * channels ) + c ] = ( row[ left + c ] * weight.a0 ) + ( row[ right + c ] * weight.a1 );
	}
}

static Plane resizeLinear( const Plane& src, const int dst_width, const int dst_height )
{
	const auto xtab { linearWeights( src.width, dst_width ) };
	const auto ytab { linearWeights( src.height, dst_height ) };
	const auto stride { static_cast< std::size_t >( dst_width ) * static_cast< std::size_t >( src.channels ) };

	Plane dst { std::vector< std::uint8_t >( stride * static_cast< std::size_t >( dst_height ) ),
		        dst_width,
		        dst_height,
		        src.channels };
	std::vector< int > top( stride );
	std::vector< int > bottom( stride );

	for ( int dy = 0; dy < dst_height; ++dy )
	{
		const auto& weight { ytab[ static_cast< std::size_t >( dy ) ] };
		horizontalLinear( src, weight.si, xtab, top );
		horizontalLinear( src, std::min( weight.si + 1, src.height - 1 ), xtab, bottom );

		auto* const out { dst.pixels.data() + ( static_cast< std::size_t >( dy ) * stride ) };
		for ( std::size_t i = 0; i < stride; ++i )
			out[ i ] = static_cast< std::uint8_t >(
				( ( ( weight.a0 * ( top[ i ] >> 4 ) ) >> 16 ) + ( ( weight.a1 * ( bottom[ i ] >> 4 ) ) >> 16 ) + 2 )
				>> 2 );
	}
	return dst;
}

//! Match OpenCV INTER_AREA resize while buffering one source row at a time.
class AreaResizer
{
	int m_src_width;
	int m_src_height;
	int m_dst_width;
	int m_dst_height;
	int m_channels;
	bool m_grows;
	std::vector< AreaWeight > m_xtab {};
	std::vector< AreaWeight > m_ytab {};
	std::size_t m_ytab_index { 0 };
	int m_row { 0 };
	std::vector< float > m_sums {};
	std::vector< float > m_buffer {};
	std::vector< std::uint8_t > m_kept {};

  public:

	AreaResizer(
		const int src_width,
		const int src_height,
		const int dst_width,
		const int dst_height,
		const int channels ) :
	  m_src_width( src_width ),
	  m_src_height( src_height ),
	  m_dst_width( dst_width ),
	  m_dst_height( dst_height ),
	  m_channels( channels ),
	  m_grows( dst_width > src_width || dst_height > src_height )
	{
		const auto stride { static_cast< std::size_t >( dst_width ) * static_cast< std::size_t >( channels ) };

		if ( m_grows )
		{
			m_kept.resize(
				static_cast< std::size_t >( src_width ) * static_cast< std::size_t >( src_height )
				* static_cast< std::size_t >( channels ) );
			return;
		}

		m_xtab = areaWeights( src_width, dst_width, channels, static_cast< double >( src_width ) / dst_width );
		m_ytab = areaWeights( src_height, dst_height, 1, static_cast< double >( src_height ) / dst_height );
		m_sums.assign( stride * static_cast< std::size_t >( dst_height ), 0.0f );
		m_buffer.assign( stride, 0.0f );
	}

	void pushRow( const std::uint8_t* row )
	{
		const auto stride { static_cast< std::size_t >( m_dst_width ) * static_cast< std::size_t >( m_channels ) };

		if ( m_grows )
		{
			const auto width { static_cast< std::size_t >( m_src_width ) * static_cast< std::size_t >( m_channels ) };
			const auto offset { static_cast< std::ptrdiff_t >( static_cast< std::size_t >( m_row ) * width ) };
			std::copy_n( row, width, m_kept.begin() + offset );
			++m_row;
			return;
		}

		while ( m_ytab_index < m_ytab.size() && m_ytab[ m_ytab_index ].si < m_row ) ++m_ytab_index;

		if ( m_ytab_index < m_ytab.size() && m_ytab[ m_ytab_index ].si == m_row )
		{
			std::ranges::fill( m_buffer, 0.0f );
			const auto channels { static_cast< std::size_t >( m_channels ) };
			for ( const auto& weight : m_xtab )
				for ( std::size_t c = 0; c < channels; ++c )
					m_buffer[ static_cast< std::size_t >( weight.di ) + c ] +=
						static_cast< float >( row[ static_cast< std::size_t >( weight.si ) + c ] ) * weight.alpha;

			for ( ; m_ytab_index < m_ytab.size() && m_ytab[ m_ytab_index ].si == m_row; ++m_ytab_index )
			{
				auto* const sums {
					m_sums.data() + ( static_cast< std::size_t >( m_ytab[ m_ytab_index ].di ) * stride )
				};
				const float beta { m_ytab[ m_ytab_index ].alpha };
				for ( std::size_t i = 0; i < stride; ++i ) sums[ i ] += m_buffer[ i ] * beta;
			}
		}

		++m_row;
	}

	[[nodiscard]] Plane finish()
	{
		if ( m_grows )
		{
			const Plane source { std::move( m_kept ), m_src_width, m_src_height, m_channels };
			return resizeLinear( source, m_dst_width, m_dst_height );
		}

		std::vector< std::uint8_t > pixels( m_sums.size() );
		for ( std::size_t i = 0; i < m_sums.size(); ++i ) pixels[ i ] = saturateByte( m_sums[ i ] );
		return Plane { std::move( pixels ), m_dst_width, m_dst_height, m_channels };
	}
};

static Plane compositeOverWhite( const Plane& rgba )
{
	const auto count { static_cast< std::size_t >( rgba.width ) * static_cast< std::size_t >( rgba.height ) };
	Plane gray { std::vector< std::uint8_t >( count ), rgba.width, rgba.height, 1 };

	for ( std::size_t i = 0; i < count; ++i )
	{
		const auto* const pixel { rgba.pixels.data() + ( i * 4 ) };
		const auto bare { static_cast< double >( luma( pixel[ 0 ], pixel[ 1 ], pixel[ 2 ] ) ) };
		const auto alpha { static_cast< double >( pixel[ 3 ] ) };
		gray.pixels[ i ] = static_cast< std::uint8_t >( ( bare * ( alpha / 255.0 ) ) + ( 255.0 - alpha ) );
	}
	return gray;
}

//! Sum data[i] * cos(pi * (2i + 1) * k / 2n). Pair symmetric terms for exact cancellation of
//! zero coefficients, matching Hydrus's FFT-based DCT.
static double cosineSum( const std::span< const double > data, const int k )
{
	const auto count { static_cast< int >( data.size() ) };

	if ( k == 0 )
	{
		double total { 0.0 };
		for ( const double value : data ) total += value;
		return total;
	}

	if ( count == 1 ) return data[ 0 ] * std::cos( std::numbers::pi * k / 2.0 );

	const auto half { data.size() / 2 };
	const bool even { k % 2 == 0 };
	std::array< double, TINY_DIMENSION / 2 > folded {};
	for ( std::size_t i = 0; i < half; ++i )
		folded[ i ] = even ? data[ i ] + data[ data.size() - 1 - i ] : data[ i ] - data[ data.size() - 1 - i ];

	const std::span< const double > next { folded.data(), half };
	if ( even ) return cosineSum( next, k / 2 );

	double total { 0.0 };
	for ( std::size_t i = 0; i < half; ++i )
		total += next[ i ]
		       * std::cos( std::numbers::pi * static_cast< double >( ( 2 * i ) + 1 ) * k / ( 2.0 * count ) );
	return total;
}

static double dctNorm( const int k )
{
	return std::sqrt( ( k == 0 ? 1.0 : 2.0 ) / TINY_DIMENSION );
}

static std::array< double, PHASH_DIMENSION * PHASH_DIMENSION > dctTopLeft( const Plane& tiny )
{
	std::array< double, TINY_DIMENSION * PHASH_DIMENSION > partial {};
	std::array< double, TINY_DIMENSION > line {};

	for ( std::size_t y = 0; y < TINY_DIMENSION; ++y )
	{
		for ( std::size_t x = 0; x < TINY_DIMENSION; ++x )
			line[ x ] = static_cast< double >( tiny.pixels[ ( y * TINY_DIMENSION ) + x ] );

		for ( std::size_t v = 0; v < PHASH_DIMENSION; ++v )
			partial[ ( y * PHASH_DIMENSION ) + v ] = dctNorm( static_cast< int >( v ) )
			                                       * cosineSum( line, static_cast< int >( v ) );
	}

	std::array< double, PHASH_DIMENSION * PHASH_DIMENSION > result {};
	for ( std::size_t v = 0; v < PHASH_DIMENSION; ++v )
	{
		for ( std::size_t y = 0; y < TINY_DIMENSION; ++y ) line[ y ] = partial[ ( y * PHASH_DIMENSION ) + v ];

		for ( std::size_t u = 0; u < PHASH_DIMENSION; ++u )
			result[ ( u * PHASH_DIMENSION ) + v ] = dctNorm( static_cast< int >( u ) )
			                                      * cosineSum( line, static_cast< int >( u ) );
	}
	return result;
}

static std::optional< PerceptualHash > hashFromTiny( const Plane& tiny )
{
	const auto dct { dctTopLeft( tiny ) };

	std::array< double, ( PHASH_DIMENSION * PHASH_DIMENSION ) - 1 > without_dc {};
	std::copy( dct.begin() + 1, dct.end(), without_dc.begin() );
	const auto middle { without_dc.begin() + ( without_dc.size() / 2 ) };
	std::nth_element( without_dc.begin(), middle, without_dc.end() );
	const double median { *middle };

	PerceptualHash hash {};
	for ( std::size_t u = 0; u < PHASH_DIMENSION; ++u )
	{
		std::uint8_t byte { 0 };
		for ( std::size_t v = 0; v < PHASH_DIMENSION; ++v )
			byte = static_cast< std::uint8_t >( ( byte << 1 ) | ( dct[ ( u * PHASH_DIMENSION ) + v ] > median ) );
		hash[ u ] = std::byte { byte };
	}

	if ( perceptualHashDistance( hash, BLANK_HASH ) <= BLANK_MAX_DISTANCE ) return std::nullopt;
	return hash;
}

struct AlphaCensus
{
	std::int64_t translucent { 0 };
	std::int64_t visible { 0 };
};

static bool alphaIsUseful( const AlphaCensus& census, const int width, const int height )
{
	const std::int64_t circumference { 2 * ( static_cast< std::int64_t >( width ) + height ) };
	const std::int64_t weight { std::max< std::int64_t >( static_cast< std::int64_t >( width ) * height / 200, 1 ) };
	const std::int64_t needed { std::min( circumference, weight ) };
	return census.translucent >= needed && census.visible >= needed;
}

static bool isRawColour( const VipsInterpretation interpretation )
{
	return interpretation == VIPS_INTERPRETATION_B_W || interpretation == VIPS_INTERPRETATION_GREY16
	    || interpretation == VIPS_INTERPRETATION_RGB || interpretation == VIPS_INTERPRETATION_RGB16
	    || interpretation == VIPS_INTERPRETATION_sRGB || interpretation == VIPS_INTERPRETATION_MULTIBAND
	    || interpretation == VIPS_INTERPRETATION_CMYK;
}

static std::expected< VipsImagePtr, ModuleError > prepareImage( VipsImage* image )
{
	g_object_ref( image );
	VipsImagePtr current { image };

	// Apply EXIF orientation to every format; Hydrus skips PNG orientation.
	VipsImage* oriented { nullptr };
	if ( vips_autorot( current.get(), &oriented, nullptr ) != 0 ) return std::unexpected( "Failed to orient image" );
	current.reset( oriented );

	if ( vips_image_get_typeof( current.get(), VIPS_META_ICC_NAME ) != 0 )
	{
		VipsImage* managed { nullptr };
		const auto transformed { vips_icc_transform(
			current.get(), &managed, "srgb", "embedded", TRUE, "intent", VIPS_INTENT_PERCEPTUAL, nullptr ) };
		if ( transformed == 0 )
			current.reset( managed );
		else
			vips_error_clear();
	}

	if ( !isRawColour( vips_image_get_interpretation( current.get() ) ) )
	{
		VipsImage* converted { nullptr };
		if ( vips_colourspace( current.get(), &converted, VIPS_INTERPRETATION_sRGB, nullptr ) != 0 )
			return std::unexpected( "Failed to convert image to sRGB" );
		current.reset( converted );
	}

	if ( vips_image_get_format( current.get() ) != VIPS_FORMAT_UCHAR )
	{
		VipsImage* narrowed { nullptr };
		if ( vips_cast( current.get(), &narrowed, VIPS_FORMAT_UCHAR, "shift", TRUE, nullptr ) != 0 )
			return std::unexpected( "Failed to reduce image to 8 bits" );
		current.reset( narrowed );
	}

	return current;
}

struct VipsRegionDeleter
{
	void operator()( VipsRegion* region ) const noexcept
	{
		if ( region ) g_object_unref( region );
	}
};

template < typename Consume >
static std::expected< void, ModuleError > readRows( VipsImage* image, Consume&& consume )
{
	constexpr int BAND_HEIGHT { 64 };

	std::unique_ptr< VipsRegion, VipsRegionDeleter > region { vips_region_new( image ) };
	if ( region == nullptr ) return std::unexpected( "Failed to open a read window on the image" );

	const int width { vips_image_get_width( image ) };
	const int height { vips_image_get_height( image ) };

	for ( int top = 0; top < height; top += BAND_HEIGHT )
	{
		VipsRect rect { .left = 0, .top = top, .width = width, .height = std::min( BAND_HEIGHT, height - top ) };
		if ( vips_region_prepare( region.get(), &rect ) != 0 ) return std::unexpected( "Failed to read image pixels" );

		for ( int y = 0; y < rect.height; ++y )
			consume( static_cast< const std::uint8_t* >( VIPS_REGION_ADDR( region.get(), 0, top + y ) ) );
	}

	return {};
}

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

	auto prepared { prepareImage( image ) };
	if ( !prepared ) return std::unexpected( prepared.error() );

	VipsImage* const source { prepared->get() };
	const int width { vips_image_get_width( source ) };
	const int height { vips_image_get_height( source ) };
	const int bands { vips_image_get_bands( source ) };
	if ( width < 1 || height < 1 || bands < 1 ) return std::unexpected( "Cannot hash an empty image" );

	// Match OpenCV's CMYK inversion for JPEGs without a color profile.
	const bool is_cmyk { bands == 4 && vips_image_get_interpretation( source ) == VIPS_INTERPRETATION_CMYK };
	const bool has_alpha { !is_cmyk && ( bands == 2 || bands >= 4 ) };

	AreaResizer gray_resizer { width, height, TINY_DIMENSION, TINY_DIMENSION, 1 };
	std::optional< AreaResizer > alpha_resizer {};
	if ( has_alpha )
		alpha_resizer.emplace(
			width, height, std::min( ALPHA_PRESCALE_MAX, width ), std::min( ALPHA_PRESCALE_MAX, height ), 4 );

	const bool is_grey { bands <= 2 };
	const auto band_stride { static_cast< std::size_t >( bands ) };

	AlphaCensus census {};
	std::vector< std::uint8_t > gray_row( static_cast< std::size_t >( width ) );
	std::vector< std::uint8_t > rgba_row( has_alpha ? static_cast< std::size_t >( width ) * 4 : 0 );

	const auto read { readRows(
		source,
		[ & ]( const std::uint8_t* row )
		{
			for ( int x = 0; x < width; ++x )
			{
				const auto* const pixel { row + ( static_cast< std::size_t >( x ) * band_stride ) };
				int r { pixel[ 0 ] };
				int g { is_grey ? pixel[ 0 ] : pixel[ 1 ] };
				int b { is_grey ? pixel[ 0 ] : pixel[ 2 ] };
				const int a { bands == 2 ? pixel[ 1 ] : ( bands >= 4 ? pixel[ 3 ] : 255 ) };

				if ( is_cmyk )
				{
					const int key { 255 - pixel[ 3 ] };
					r = ( ( 255 - r ) * key ) / 255;
					g = ( ( 255 - g ) * key ) / 255;
					b = ( ( 255 - b ) * key ) / 255;
				}

				gray_row[ static_cast< std::size_t >( x ) ] = luma( r, g, b );

				if ( !has_alpha ) continue;

				auto* const out { rgba_row.data() + ( static_cast< std::size_t >( x ) * 4 ) };
				out[ 0 ] = static_cast< std::uint8_t >( r );
				out[ 1 ] = static_cast< std::uint8_t >( g );
				out[ 2 ] = static_cast< std::uint8_t >( b );
				out[ 3 ] = static_cast< std::uint8_t >( a );

				census.translucent += a < ALPHA_OPAQUE_FLOOR;
				census.visible += a > ALPHA_TRANSPARENT_CEILING;
			}

			gray_resizer.pushRow( gray_row.data() );
			if ( alpha_resizer ) alpha_resizer->pushRow( rgba_row.data() );
		} ) };
	if ( !read ) return std::unexpected( read.error() );

	if ( !has_alpha || !alphaIsUseful( census, width, height ) ) return hashFromTiny( gray_resizer.finish() );

	const Plane composited { compositeOverWhite( alpha_resizer->finish() ) };
	AreaResizer tiny_resizer { composited.width, composited.height, TINY_DIMENSION, TINY_DIMENSION, 1 };
	for ( int y = 0; y < composited.height; ++y ) tiny_resizer.pushRow( composited.row( y ) );
	return hashFromTiny( tiny_resizer.finish() );
}

} // namespace idhan
