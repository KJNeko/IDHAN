//
// Created by kj16609 on 11/14/25.
//
#pragma once

extern "C" {
#include <libavformat/avio.h>
}

#include <cstdint>
#include <string_view>

struct OpaqueInfo
{
	std::string_view m_data;
	std::int64_t m_cursor { 0 };
};

inline int readFunction( void* opaque, std::uint8_t* buffer, int buffer_size )
{
	auto& buffer_view { *static_cast< OpaqueInfo* >( opaque ) };

	auto* data { buffer_view.m_data.data() };
	if ( buffer_view.m_cursor >= buffer_view.m_data.size() ) return 0;

	data += buffer_view.m_cursor;
	const std::int64_t size { static_cast< std::int64_t >( buffer_view.m_data.size() ) - buffer_view.m_cursor };
	const std::int64_t min_size { std::min( size, static_cast< std::int64_t >( buffer_size ) ) };
	std::memcpy( buffer, data, min_size );

	buffer_view.m_cursor += min_size;

	return min_size;
}

inline std::int64_t seekFunction( void* opaque, std::int64_t offset, int whence )
{
	auto& buffer_view { *static_cast< OpaqueInfo* >( opaque ) };

	switch ( whence )
	{
		case SEEK_SET:
			buffer_view.m_cursor = offset;
			break;
		case SEEK_CUR:
			buffer_view.m_cursor += offset;
			break;
		case SEEK_END:
			buffer_view.m_cursor = static_cast< std::int64_t >( buffer_view.m_data.size() ) + offset;
			break;
		case AVSEEK_SIZE:
			return static_cast< std::int64_t >( buffer_view.m_data.size() );
		default:
			return -1;
	}

	return buffer_view.m_cursor;
}

inline static std::vector< std::string_view >
	ffmpeg_handleable_mimes { "video/mp4", "video/webm", "video/mpeg", "video/quicktime" };
