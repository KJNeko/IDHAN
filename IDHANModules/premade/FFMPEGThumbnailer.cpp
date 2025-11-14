//
// Created by kj16609 on 11/13/25.
//
#include "FFMPEGThumbnailer.hpp"

#include <vips/vips8>

#include <cstring>
#include <memory>
#include <vector>

#include "ffmpeg.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <libswscale/swscale.h>
}

std::string_view FFMPEGThumbnailer::name()
{
	return "FFMPEGThumbnailer";
}

idhan::ModuleVersion FFMPEGThumbnailer::version()
{
	return idhan::ModuleVersion { .m_major = 0, .m_minor = 1, .m_patch = 0 };
}

std::vector< std::string_view > FFMPEGThumbnailer::handleableMimes()
{
	return ffmpeg_handleable_mimes;
}

std::expected< idhan::ThumbnailerModuleI::ThumbnailInfo, idhan::ModuleError > FFMPEGThumbnailer::createThumbnail(
	const void* data,
	std::size_t length,
	std::size_t width,
	std::size_t height,
	std::string mime_name )
{
	OpaqueInfo opaque_info { .m_data = std::string_view( static_cast< const char* >( data ), length ), .m_cursor = 0 };

	constexpr auto BUFFER_SIZE { 4096 };
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
		[ buffer_ptr ]( AVIOContext* ctx )
		{
			if ( ctx )
			{
				av_free( ctx );
			}
			delete[] buffer_ptr;
		} );

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

	// Find video stream
	int video_stream_index = -1;
	AVCodecParameters* codec_params = nullptr;
	for ( unsigned int i = 0; i < format_context->nb_streams; i++ )
	{
		if ( format_context->streams[ i ]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO )
		{
			video_stream_index = i;
			codec_params = format_context->streams[ i ]->codecpar;
			break;
		}
	}

	if ( video_stream_index == -1 )
	{
		return std::unexpected( idhan::ModuleError( "No video stream found" ) );
	}

	// Calculate 10% duration and seek to it
	const auto duration { av_rescale_q(
		format_context->duration, AV_TIME_BASE_Q, format_context->streams[ video_stream_index ]->time_base ) };
	const auto target_timestamp { static_cast< int64_t >( duration * 0.10 ) };

	if ( av_seek_frame( format_context, video_stream_index, target_timestamp, AVSEEK_FLAG_BACKWARD ) < 0 )
	{
		return std::unexpected( idhan::ModuleError( "Failed to seek to timestamp" ) );
	}

	// Find and open codec
	const AVCodec* codec = avcodec_find_decoder( codec_params->codec_id );
	if ( !codec )
	{
		return std::unexpected( idhan::ModuleError( "Codec not found" ) );
	}

	AVCodecContext* codec_context = avcodec_alloc_context3( codec );
	if ( avcodec_parameters_to_context( codec_context, codec_params ) < 0 )
	{
		avcodec_free_context( &codec_context );
		return std::unexpected( idhan::ModuleError( "Failed to copy codec params" ) );
	}

	if ( avcodec_open2( codec_context, codec, nullptr ) < 0 )
	{
		avcodec_free_context( &codec_context );
		return std::unexpected( idhan::ModuleError( "Failed to open codec" ) );
	}

	// Read frames until we get a valid video frame
	AVPacket* packet = av_packet_alloc();
	AVFrame* frame = av_frame_alloc();
	bool frame_decoded = false;

	while ( av_read_frame( format_context, packet ) >= 0 )
	{
		if ( packet->stream_index == video_stream_index )
		{
			if ( avcodec_send_packet( codec_context, packet ) >= 0 )
			{
				if ( avcodec_receive_frame( codec_context, frame ) >= 0 )
				{
					frame_decoded = true;
					av_packet_unref( packet );
					break;
				}
			}
		}
		av_packet_unref( packet );
	}

	if ( !frame_decoded )
	{
		av_frame_free( &frame );
		av_packet_free( &packet );
		avcodec_free_context( &codec_context );
		return std::unexpected( idhan::ModuleError( "Failed to decode frame" ) );
	}

	// Convert frame to RGB24 using swscale
	SwsContext* sws_context = sws_getContext(
		codec_context->width,
		codec_context->height,
		codec_context->pix_fmt,
		codec_context->width,
		codec_context->height,
		AV_PIX_FMT_RGB24,
		SWS_BILINEAR,
		nullptr,
		nullptr,
		nullptr );

	AVFrame* rgb_frame = av_frame_alloc();
	rgb_frame->format = AV_PIX_FMT_RGB24;
	rgb_frame->width = codec_context->width;
	rgb_frame->height = codec_context->height;
	av_frame_get_buffer( rgb_frame, 0 );

	sws_scale(
		sws_context, frame->data, frame->linesize, 0, codec_context->height, rgb_frame->data, rgb_frame->linesize );

	VipsImage* image { vips_image_new_from_memory(
		rgb_frame->data[ 0 ],
		rgb_frame->linesize[ 0 ] * rgb_frame->height,
		rgb_frame->width,
		rgb_frame->height,
		3,
		VIPS_FORMAT_UCHAR ) };

	const float source_aspect { static_cast< float >( rgb_frame->width ) / static_cast< float >( rgb_frame->height ) };
	const float target_aspect { static_cast< float >( width ) / static_cast< float >( height ) };

	if ( target_aspect > source_aspect )
		width = static_cast< std::size_t >( static_cast< float >( height ) * source_aspect );
	else
		height = static_cast< std::size_t >( static_cast< float >( width ) / source_aspect );

	VipsImage* resized { nullptr };
	if ( vips_resize(
			 image,
			 &resized,
			 static_cast< double >( width ) / static_cast< double >( vips_image_get_width( image ) ),
			 nullptr ) )
	{
		g_object_unref( image );
		return std::unexpected( idhan::ModuleError { "Failed to resize image" } );
	}
	g_object_unref( image );

	// Encode to PNG
	void* output_buffer { nullptr };
	std::size_t output_length { 0 };
	if ( vips_pngsave_buffer( resized, &output_buffer, &output_length, nullptr ) )
	{
		g_object_unref( resized );
		return std::unexpected( idhan::ModuleError( "Failed to resize image" ) );
	}
	g_object_unref( resized );

	const std::vector< std::byte > output(
		static_cast< std::byte* >( output_buffer ), static_cast< std::byte* >( output_buffer ) + output_length );
	g_free( output_buffer );

	// Cleanup
	sws_freeContext( sws_context );
	av_frame_free( &rgb_frame );
	av_frame_free( &frame );
	av_packet_free( &packet );
	avcodec_free_context( &codec_context );

	return idhan::ThumbnailerModuleI::ThumbnailInfo { .data = std::move( output ), .width = width, .height = height };
}
