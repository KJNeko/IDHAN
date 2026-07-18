//
// Created by kj16609 on 11/25/25.
//
#pragma once

#include <vips/vips8>

#include <cstdint>
#include <vector>

namespace idhan
{

//! A generated thumbnail, either raw RGB pixels or an encoded PNG (see m_mode).
struct ThumbnailInfo
{
	std::vector< std::byte > m_pixel_data {}; //!< The thumbnail bytes; interpretation depends on m_mode.
	std::size_t width, height; //!< Thumbnail dimensions in pixels.
	bool cache_thumbnail { true }; //!< Whether the host should persist this thumbnail to its cache.

	//! Convenience value for the cache_thumbnail argument, expressing "do not cache".
	static constexpr auto NOCACHE { false };

	//! Builds a RAW thumbnail from a libvips image.
	//! \param image Source image whose pixels are copied out.
	//! \param cache_thumbnail Whether the host should cache the result.
	ThumbnailInfo( vips::VImage& image, bool cache_thumbnail = true );

	ThumbnailInfo() : width( 0 ), height( 0 ) {}
};

} // namespace idhan