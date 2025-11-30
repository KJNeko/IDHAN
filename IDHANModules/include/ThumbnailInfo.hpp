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
	bool cache_thumbnail { true };

	enum ThumbnailMode
	{
		RAW,
		FILE_PNG
	} m_mode;

	static constexpr auto NOCACHE { false };

	ThumbnailInfo( vips::VImage& image, bool cache_thumbnail = true );

	ThumbnailInfo() : width( 0 ), height( 0 ), m_mode( FILE_PNG ) {}
};

} // namespace idhan