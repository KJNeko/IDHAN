//
// Created by kj16609 on 11/12/25.
//
#include "VideoMetadata.hpp"

#include <array>
#include <cstring>
#include <iostream>
#include <memory>

#include "fgl/defines.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

inline static std::vector< std::string_view > ffmpeg_handleable_mimes { "video/mp4" };

std::string_view VideoMetadata::name()
{
	return "Video Metadata Module";
}

idhan::ModuleVersion VideoMetadata::version()
{
	return { .m_major = 1, .m_minor = 0, .m_patch = 0 };
}

std::vector< std::string_view > VideoMetadata::handleableMimes()
{
	return ffmpeg_handleable_mimes;
}

struct OpaqueInfo
{
	std::string_view m_data;
	std::int64_t m_cursor { 0 };
};

// copies
int readFunction( void* opaque, std::uint8_t* buffer, int buffer_size )
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

// Add after readFunction
std::int64_t seekFunction( void* opaque, std::int64_t offset, int whence )
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

std::expected< idhan::MetadataInfo, idhan::ModuleError > VideoMetadata::parseFile(
	const void* data,
	std::size_t length,
	std::string mime_name )
{
	idhan::MetadataInfo base_info {};
	idhan::MetadataInfoVideo video_metadata {};

	OpaqueInfo opaque_info { .m_data = std::string_view( static_cast< const char* >( data ), length ), .m_cursor = 0 };

	constexpr auto BUFFER_SIZE { 4096 };
	// std::array< std::byte, BUFFER_SIZE > buffer {};
	std::byte* buffer_ptr { new std::byte[ BUFFER_SIZE ] };

	const std::shared_ptr< AVIOContext > avio_context(
		avio_alloc_context(
			reinterpret_cast< unsigned char* >( buffer_ptr ),
			BUFFER_SIZE,
			0,
			&opaque_info,
			&readFunction,
			nullptr,
			seekFunction ),
		&av_free );

	// auto format_context_u { std::unique_ptr< AVFormatContext, void ( * )( AVFormatContext* ) >(
	// ::avformat_alloc_context(), freeAVFormatContext ) };

	// auto format_context { format_context_u.get() };
	const auto format_context_p =
		std::shared_ptr< AVFormatContext >( avformat_alloc_context(), &avformat_free_context );
	auto format_context { format_context_p.get() };

	format_context->pb = avio_context.get();
	format_context->flags |= AVFMT_FLAG_CUSTOM_IO;

	if ( avformat_open_input( &format_context, "", nullptr, nullptr ) < 0 )
	{
		return std::unexpected( idhan::ModuleError( "Failed to open file" ) );
	}

	if ( avformat_find_stream_info( format_context, nullptr ) < 0 )
	{
		return std::unexpected( idhan::ModuleError( "Failed to find stream info" ) );
	}

	bool has_video { false };
	bool has_audio { false };

	for ( unsigned int i = 0; i < format_context->nb_streams; ++i )
	{
		AVStream* stream = format_context->streams[ i ];
		AVCodecParameters* codecpar = stream->codecpar;

		if ( codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !has_video )
		{
			has_video = true;
			video_metadata.m_width = codecpar->width;
			video_metadata.m_height = codecpar->height;

			// Calculate bitrate (prefer codec bitrate, fall back to container bitrate)
			if ( codecpar->bit_rate > 0 )
			{
				video_metadata.m_bitrate = codecpar->bit_rate;
			}
			else if ( format_context->bit_rate > 0 )
			{
				// Fall back to container bitrate if no stream-specific bitrate
				video_metadata.m_bitrate = format_context->bit_rate;
			}

			// Get duration (in seconds)
			if ( stream->duration != AV_NOPTS_VALUE )
			{
				video_metadata.m_duration = static_cast< double >( stream->duration ) * av_q2d( stream->time_base );
			}
			else if ( format_context->duration != AV_NOPTS_VALUE )
			{
				video_metadata.m_duration = static_cast< double >( format_context->duration ) / AV_TIME_BASE;
			}

			// Get frame rate
			if ( stream->avg_frame_rate.den > 0 )
			{
				video_metadata.m_fps = av_q2d( stream->avg_frame_rate );
			}
		}
		else if ( codecpar->codec_type == AVMEDIA_TYPE_AUDIO )
		{
			has_audio = true;
		}
	}

	video_metadata.m_has_audio = has_audio;

	base_info.m_metadata = video_metadata;

	return base_info;
}