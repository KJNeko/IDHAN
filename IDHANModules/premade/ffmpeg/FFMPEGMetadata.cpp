//
// Created by kj16609 on 11/12/25.
//

#include "FFMPEGMetadata.hpp"

#include <cstring>
#include <iostream>
#include <memory>

#include "ffmpeg.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

std::string_view FFMPEGMetadata::name()
{
	return "Video Metadata Module";
}

idhan::ModuleVersion FFMPEGMetadata::version()
{
	return { .m_major = 1, .m_minor = 0, .m_patch = 0 };
}

std::vector< std::string_view > FFMPEGMetadata::handleableMimes()
{
	return ffmpeg_handleable_mimes;
}

std::expected< idhan::MetadataInfo, idhan::ModuleError > FFMPEGMetadata::parseFile( idhan::ModuleCallData& data )
{
	idhan::MetadataInfo base_info {};
	base_info.m_simple_type = idhan::SimpleMimeType::VIDEO;
	idhan::MetadataInfoVideo video_metadata {};

	OpaqueInfo opaque_info { .m_data = data.file_view, .m_cursor = 0 };

	constexpr auto BUFFER_SIZE { 4096 };
	// std::array< std::byte, BUFFER_SIZE > buffer {};
	// std::byte* buffer_ptr { new std::byte[ BUFFER_SIZE ] };
	unsigned char* buffer_ptr { static_cast< unsigned char* >( av_malloc( BUFFER_SIZE ) ) };

	// the buffer may have been freed and replaced by libavformat, so the deleter must free
	// ctx->buffer rather than the original allocation
	const std::shared_ptr< AVIOContext > avio_context(
		avio_alloc_context( buffer_ptr, BUFFER_SIZE, 0, &opaque_info, &readFunction, nullptr, seekFunction ),
		[]( AVIOContext* ctx )
		{
			if ( ctx )
			{
				av_free( ctx->buffer );
				av_free( ctx );
			}
		} );

	if ( !avio_context )
	{
		// avio_alloc_context does not take ownership of the buffer on failure, so free it here
		// (the deleter above is a no-op for a null context).
		av_free( buffer_ptr );
		return std::unexpected( idhan::ModuleError( "Failed to allocate AVIO context" ) );
	}

	AVFormatContext* format_context { avformat_alloc_context() };
	if ( !format_context )
	{
		return std::unexpected( idhan::ModuleError( "Failed to allocate format context" ) );
	}

	format_context->pb = avio_context.get();
	format_context->flags |= AVFMT_FLAG_CUSTOM_IO;

	// avformat_open_input frees format_context itself on failure; only take ownership once it
	// succeeds, otherwise the shared_ptr below would double-free it
	if ( avformat_open_input( &format_context, "", nullptr, nullptr ) < 0 )
	{
		return std::unexpected( idhan::ModuleError( "Failed to open file" ) );
	}

	// avformat_close_input (not avformat_free_context) is the correct counterpart to a successful
	// avformat_open_input: it runs the demuxer's read_close and releases internal buffers.
	// AVFMT_FLAG_CUSTOM_IO is set, so it leaves the custom pb (avio_context) for us to free.
	const auto format_context_p = std::shared_ptr< AVFormatContext >(
		format_context, []( AVFormatContext* ctx ) { avformat_close_input( &ctx ); } );
	format_context = format_context_p.get();

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