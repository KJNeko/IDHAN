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
	std::vector< std::byte > data {}; //!< The thumbnail bytes; interpretation depends on m_mode.
	std::size_t width, height; //!< Thumbnail dimensions in pixels.
	bool cache_thumbnail { true }; //!< Whether the host should persist this thumbnail to its cache.

	//! How to interpret ::data.
	enum ThumbnailMode
	{
		RAW, //!< Raw interleaved RGB pixels.
		FILE_PNG //!< A complete in-memory PNG file.
	} m_mode;

	//! Convenience value for the cache_thumbnail argument, expressing "do not cache".
	static constexpr auto NOCACHE { false };

	//! Builds a RAW thumbnail from a libvips image.
	//! \param image Source image whose pixels are copied out.
	//! \param cache_thumbnail Whether the host should cache the result.
	ThumbnailInfo( vips::VImage& image, bool cache_thumbnail = true );

	//! Constructs an empty FILE_PNG thumbnail (zero-sized), to be filled in by the caller.
	ThumbnailInfo() : width( 0 ), height( 0 ), m_mode( FILE_PNG ) {}
};

} // namespace idhan