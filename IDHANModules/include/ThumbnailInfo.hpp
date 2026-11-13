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

enum class ThumbnailFormat : std::uint8_t
{
	RGB, //!< Raw interleaved RGB, width * height * 3 bytes.
	ANIMATED, //!< A complete encoded animation; width and height describe one frame.
	ENCODED, //!< A complete encoded still image; width and height describe it.
};

struct ThumbnailInfo
{
	std::vector< std::byte > m_pixel_data {}; //!< The thumbnail bytes; interpretation depends on m_format.
	std::size_t width, height;
	bool cache_thumbnail{ true };
	ThumbnailFormat m_format{ ThumbnailFormat::RGB };

	static constexpr auto NOCACHE { false };

	ThumbnailInfo() : width( 0 ), height( 0 ) {}

	ThumbnailInfo( VipsImagePtr&& image, bool do_cache_thumbnail = true );

	~ThumbnailInfo() = default;

	FGL_DEFAULT_COPY( ThumbnailInfo );
	FGL_DEFAULT_MOVE( ThumbnailInfo );
};

} // namespace idhan