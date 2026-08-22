#pragma once

#include <vips/vips8>

#include <cstdint>
#include <memory>
#include <vector>

#include "fgl/defines.hpp"

namespace idhan
{

struct VipsImageDeleter
{
	void operator()( VipsImage* image ) const noexcept
	{
		if ( image )
		{
			g_object_unref( image );
		}
	}
};

using VipsImagePtr = std::unique_ptr< VipsImage, VipsImageDeleter >;

//! How to read ThumbnailInfo::m_pixel_data.
enum class ThumbnailFormat : std::uint8_t
{
	RGB, //!< Raw interleaved RGB, width * height * 3 bytes.
	ANIMATED, //!< A complete encoded animation; width and height describe one frame.
};

//! A generated thumbnail. m_format says whether m_pixel_data is raw RGB pixels or an encoded animation.
struct ThumbnailInfo
{
	std::vector< std::byte > m_pixel_data {}; //!< The thumbnail bytes; interpretation depends on m_format.
	std::size_t width, height; //!< Thumbnail dimensions in pixels.
	bool cache_thumbnail { true }; //!< Whether the host should persist this thumbnail to its cache.
	ThumbnailFormat m_format { ThumbnailFormat::RGB }; //!< How to read m_pixel_data.

	//! Convenience value for the cache_thumbnail argument, expressing "do not cache".
	static constexpr auto NOCACHE { false };

	ThumbnailInfo() : width( 0 ), height( 0 ) {}

	ThumbnailInfo( VipsImagePtr&& image, bool do_cache_thumbnail = true );

	~ThumbnailInfo() = default;

	FGL_DEFAULT_COPY( ThumbnailInfo );
	FGL_DEFAULT_MOVE( ThumbnailInfo );
};

} // namespace idhan