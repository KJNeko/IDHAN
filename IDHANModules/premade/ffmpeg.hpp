//
// Created by kj16609 on 11/14/25.
//
#pragma once

#include <libavutil/error.h>
#include <spdlog/spdlog.h>

extern "C" {
#include <libavformat/avio.h>
}

#include <cstdint>
#include <string_view>

namespace idhan
{
namespace log = spdlog;
}

struct OpaqueInfo
{
	std::string_view m_data;
	std::int64_t m_cursor { 0 };
};

inline int readFunction( void* opaque, std::uint8_t* buffer, int buffer_size )
{
	auto& buffer_view { *static_cast< OpaqueInfo* >( opaque ) };

	const bool cursor_oob { buffer_view.m_cursor > buffer_view.m_data.size() };
	if ( cursor_oob ) return AVERROR_EOF;

	auto* data { buffer_view.m_data.data() };

	data += buffer_view.m_cursor;
	const std::int64_t size { static_cast< std::int64_t >( buffer_view.m_data.size() ) - buffer_view.m_cursor };
	const std::int64_t min_size { std::min( size, static_cast< std::int64_t >( buffer_size ) ) };

	if ( min_size == 0 ) return AVERROR_EOF;

	std::memcpy( buffer, data, min_size );

	buffer_view.m_cursor += min_size;

	return static_cast< int >( min_size );
}

inline std::int64_t seekFunction( void* opaque, std::int64_t offset, int whence )
{
	auto& buffer_view { *static_cast< OpaqueInfo* >( opaque ) };

	idhan::log::trace( "Asked to seek from whence {} and offset {}", whence, offset );
	switch ( whence )
	{
		case SEEK_SET:
			idhan::log::trace( "Asked to seek to specific offset {}", offset );
			buffer_view.m_cursor = offset;
			break;
		case SEEK_CUR:
			idhan::log::trace( "Asked to seek to an +{} from cursor", offset );
			buffer_view.m_cursor += offset;
			break;
		case SEEK_END:
			idhan::log::trace( "Asked to seek to end" );
			buffer_view.m_cursor = static_cast< std::int64_t >( buffer_view.m_data.size() ) + offset;
			break;
		case AVSEEK_SIZE:
			idhan::log::trace( "Asked to seek size" );
			return static_cast< std::int64_t >( buffer_view.m_data.size() );
		default:
			{
				idhan::log::warn( "Asked to seek to whence that ended in default" );
				return -1;
			}
	}

	if ( buffer_view.m_cursor < 0 )
		buffer_view.m_cursor = 0;
	else if ( buffer_view.m_cursor >= buffer_view.m_data.size() )
		buffer_view.m_cursor = buffer_view.m_data.size();

	return buffer_view.m_cursor;
}

inline static std::vector< std::string_view >
	ffmpeg_handleable_mimes { "video/mp4", "video/webm", "video/mpeg", "video/quicktime" };
