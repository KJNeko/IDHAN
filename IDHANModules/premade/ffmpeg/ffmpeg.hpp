#pragma once

#include "MimeIDs.hpp"
#include <libavutil/error.h>
#include <spdlog/spdlog.h>

extern "C" {
#include <libavformat/avio.h>
}

#include <cstdint>
#include <span>
#include <string_view>

#include "ModuleBase.hpp"

namespace idhan
{
namespace log = spdlog;
}

//! State backing a custom FFmpeg AVIO context: the file being demuxed and a read/seek cursor.
struct OpaqueInfo
{
	const idhan::ModuleFile* m_file { nullptr }; //!< The file being read (not owned).
	std::int64_t m_cursor { 0 }; //!< Current read position, in bytes from the start.
};

//! AVIO read callback: fills up to \p buffer_size bytes from the OpaqueInfo cursor into \p buffer.
//! \param opaque Pointer to the OpaqueInfo.
//! \return Number of bytes read, AVERROR_EOF at end of data, or AVERROR(EIO) if the read failed.
inline int readFunction( void* opaque, std::uint8_t* buffer, const int buffer_size )
{
	auto& state { *static_cast< OpaqueInfo* >( opaque ) };

	if ( buffer_size <= 0 || state.m_cursor < 0 ) return AVERROR_EOF;

	const auto size { static_cast< std::int64_t >( state.m_file->size() ) };
	if ( state.m_cursor >= size ) return AVERROR_EOF;

	const auto count { state.m_file->read(
		std::span< std::byte > { reinterpret_cast< std::byte* >( buffer ), static_cast< std::size_t >( buffer_size ) },
		static_cast< std::size_t >( state.m_cursor ) ) };

	if ( !count )
	{
		idhan::log::warn( "Read of the input failed: {}", count.error() );
		return AVERROR( EIO );
	}

	if ( *count == 0 ) return AVERROR_EOF;

	state.m_cursor += static_cast< std::int64_t >( *count );

	return static_cast< int >( *count );
}

//! AVIO seek callback: moves the OpaqueInfo cursor, clamped to the file bounds.
//! \param opaque Pointer to the OpaqueInfo.
//! \param offset Target offset, interpreted relative to \p whence.
//! \param whence SEEK_SET/SEEK_CUR/SEEK_END, or AVSEEK_SIZE to query the total size.
//! \return The new cursor position, the size for AVSEEK_SIZE, or -1 for an unsupported \p whence.
inline std::int64_t seekFunction( void* opaque, std::int64_t offset, int whence )
{
	auto& state { *static_cast< OpaqueInfo* >( opaque ) };

	const auto size { static_cast< std::int64_t >( state.m_file->size() ) };

	idhan::log::trace( "Asked to seek from whence {} and offset {}", whence, offset );
	switch ( whence )
	{
		case SEEK_SET:
			idhan::log::trace( "Asked to seek to specific offset {}", offset );
			state.m_cursor = offset;
			break;
		case SEEK_CUR:
			idhan::log::trace( "Asked to seek to an +{} from cursor", offset );
			state.m_cursor += offset;
			break;
		case SEEK_END:
			idhan::log::trace( "Asked to seek to end" );
			state.m_cursor = size + offset;
			break;
		case AVSEEK_SIZE:
			idhan::log::trace( "Asked to seek size" );
			return size;
		default:
			{
				idhan::log::warn( "Asked to seek to whence that ended in default" );
				return -1;
			}
	}

	if ( state.m_cursor < 0 )
		state.m_cursor = 0;
	else if ( state.m_cursor >= size )
		state.m_cursor = size;

	return state.m_cursor;
}

//! The mime ids the FFmpeg thumbnailer/metadata modules advertise support for.
inline static std::vector< idhan::MimeID > ffmpeg_handleable_mimes {
	idhan::mime_ids::VIDEO_MP4,
	idhan::mime_ids::VIDEO_WEBM,
	idhan::mime_ids::VIDEO_MPEG,
	idhan::mime_ids::VIDEO_QUICKTIME
};
