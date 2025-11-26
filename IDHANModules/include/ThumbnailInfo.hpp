//
// Created by kj16609 on 11/25/25.
//
#pragma once

#include <vips/vips8>

#include <cstdint>
#include <vector>

namespace idhan
{

struct ThumbnailInfo
{
	std::vector< std::byte > data {};
	std::size_t width, height;

	enum ThumbnailMode
	{
		RAW,
		FILE_PNG
	} m_mode;

	ThumbnailInfo( vips::VImage& image );

	ThumbnailInfo() : width( 0 ), height( 0 ), m_mode( FILE_PNG ) {}
};

} // namespace idhan