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

#include "ModuleBase.hpp"

namespace idhan
{
namespace log = spdlog;
}

//! State backing a custom FFmpeg AVIO context: an in-memory file and a read/seek cursor. Passed as
//! the opaque pointer to readFunction/seekFunction so FFmpeg can demux a buffer without touching disk.
struct OpaqueInfo
{
	idhan::data_view m_data; //!< The in-memory file bytes (not owned).
	std::int64_t m_cursor { 0 }; //!< Current read position, in bytes from the start.
};

//! AVIO read callback: copies up to \p buffer_size bytes from the OpaqueInfo cursor into \p buffer.
//! \param opaque Pointer to the OpaqueInfo.
//! \return Number of bytes read, or AVERROR_EOF at end of data.
inline int readFunction( void* opaque, std::uint8_t* buffer, const int buffer_size )
{
	auto& buffer_view { *static_cast< OpaqueInfo* >( opaque ) };

	const bool cursor_oob { buffer_view.m_cursor > static_cast< std::int64_t >( buffer_view.m_data.size() ) };
	if ( cursor_oob ) return AVERROR_EOF;

	auto* data { buffer_view.m_data.data() };

	data += buffer_view.m_cursor;
	const std::int64_t size { static_cast< std::int64_t >( buffer_view.m_data.size() ) - buffer_view.m_cursor };
	const std::int64_t min_size { std::min( size, static_cast< std::int64_t >( buffer_size ) ) };

	if ( min_size == 0 ) return AVERROR_EOF;

	std::memcpy( buffer, data, static_cast< std::size_t >( min_size ) );

	buffer_view.m_cursor += min_size;

	return static_cast< int >( min_size );
}

//! AVIO seek callback: moves the OpaqueInfo cursor, clamped to the buffer bounds.
//! \param opaque Pointer to the OpaqueInfo.
//! \param offset Target offset, interpreted relative to \p whence.
//! \param whence SEEK_SET/SEEK_CUR/SEEK_END, or AVSEEK_SIZE to query the total size.
//! \return The new cursor position, the size for AVSEEK_SIZE, or -1 for an unsupported \p whence.
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
	else if ( buffer_view.m_cursor >= static_cast< std::int64_t >( buffer_view.m_data.size() ) )
		buffer_view.m_cursor = static_cast< std::int64_t >( buffer_view.m_data.size() );

	return buffer_view.m_cursor;
}

//! The canonical MIME types the FFmpeg thumbnailer/metadata modules advertise support for.
inline static std::vector< std::string_view >
	ffmpeg_handleable_mimes { "video/mp4", "video/webm", "video/mpeg", "video/quicktime" };
