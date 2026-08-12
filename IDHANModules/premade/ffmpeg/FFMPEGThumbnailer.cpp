#include "FFMPEGThumbnailer.hpp"

#include <vips/vips.h>

#include <algorithm>
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

// Custom deleters for FFmpeg types
struct AVFormatContextDeleter
{
	void operator()( AVFormatContext* ctx ) const
	{
		if ( ctx )
		{
			avformat_close_input( &ctx );
			avformat_free_context( ctx );
		}
	}
};

struct AVCodecContextDeleter
{
	void operator()( AVCodecContext* ctx ) const
	{
		if ( ctx )
		{
			avcodec_free_context( &ctx );
		}
	}
};

struct AVPacketDeleter
{
	void operator()( AVPacket* pkt ) const
	{
		if ( pkt )
		{
			av_packet_free( &pkt );
		}
	}
};

struct AVFrameDeleter
{
	void operator()( AVFrame* frame ) const
	{
		if ( frame )
		{
			av_frame_free( &frame );
		}
	}
};

struct SwsContextDeleter
{
	void operator()( SwsContext* ctx ) const
	{
		if ( ctx )
		{
			sws_freeContext( ctx );
		}
	}
};

struct AVIOContextDeleter
{
	void operator()( AVIOContext* ctx ) const
	{
		if ( ctx )
		{
			// the buffer may have been freed and replaced by libavformat, so free ctx->buffer
			// rather than the original allocation
			av_free( ctx->buffer );
			av_free( ctx );
		}
	}
};

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

std::expected< idhan::ThumbnailInfo, idhan::ModuleError > FFMPEGThumbnailer::createThumbnailRaw(
	idhan::ModuleCallData& data,
	std::size_t width,
	std::size_t height )
{
	OpaqueInfo opaque_info { .m_file = &data.file, .m_cursor = 0 };

	constexpr auto BUFFER_SIZE { 4096 };
	// must be av_malloc'd: libavformat may free()/realloc() this buffer internally, and freeing
	// operator new[] memory through ffmpeg's allocator (or vice versa) is UB
	auto* buffer_ptr { static_cast< std::byte* >( av_malloc( BUFFER_SIZE ) ) };

	std::unique_ptr< AVIOContext, AVIOContextDeleter > avio_context( avio_alloc_context(
		reinterpret_cast< unsigned char* >( buffer_ptr ),
		BUFFER_SIZE,
		0,
		&opaque_info,
		&readFunction,
		nullptr,
		seekFunction ) );

	if ( !avio_context )
	{
		return std::unexpected( idhan::ModuleError( "Failed to allocate AVIO context" ) );
	}

	AVFormatContext* format_context_raw { avformat_alloc_context() };
	if ( !format_context_raw )
	{
		return std::unexpected( idhan::ModuleError( "Failed to allocate format context" ) );
	}

	format_context_raw->pb = avio_context.get();
	format_context_raw->flags |= AVFMT_FLAG_CUSTOM_IO;

	// avformat_open_input frees format_context_raw itself on failure; only take ownership once
	// it succeeds, otherwise the unique_ptr below would double-free it
	if ( avformat_open_input( &format_context_raw, "", nullptr, nullptr ) < 0 )
	{
		return std::unexpected( idhan::ModuleError( "Failed to open file" ) );
	}

	std::unique_ptr< AVFormatContext, AVFormatContextDeleter > format_context( format_context_raw );

	if ( avformat_find_stream_info( format_context.get(), nullptr ) < 0 )
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
			video_stream_index = static_cast< int >( i );
			codec_params = format_context->streams[ i ]->codecpar;
			break;
		}
	}

	if ( video_stream_index == -1 )
	{
		return std::unexpected( idhan::ModuleError( "No video stream found" ) );
	}

	// Calculate 10% duration and seek to it
	const auto& stream { format_context->streams[ video_stream_index ] };
	const auto duration { av_rescale_q( format_context->duration, AV_TIME_BASE_Q, stream->time_base ) };
	const auto target_timestamp { duration / 10 }; // 10% of the total duration

	if ( av_seek_frame( format_context.get(), video_stream_index, target_timestamp, AVSEEK_FLAG_BACKWARD ) < 0 )
	{
		return std::unexpected( idhan::ModuleError( "Failed to seek to timestamp" ) );
	}

	// Find and open codec
	const AVCodec* codec = avcodec_find_decoder( codec_params->codec_id );
	if ( !codec )
	{
		return std::unexpected( idhan::ModuleError( "Codec not found" ) );
	}

	std::unique_ptr< AVCodecContext, AVCodecContextDeleter > codec_context( avcodec_alloc_context3( codec ) );
	if ( !codec_context )
	{
		return std::unexpected( idhan::ModuleError( "Failed to allocate codec context" ) );
	}

	if ( avcodec_parameters_to_context( codec_context.get(), codec_params ) < 0 )
	{
		return std::unexpected( idhan::ModuleError( "Failed to copy codec params" ) );
	}

	if ( avcodec_open2( codec_context.get(), codec, nullptr ) < 0 )
	{
		return std::unexpected( idhan::ModuleError( "Failed to open codec" ) );
	}

	// Read frames until we get a valid video frame
	const std::unique_ptr< AVPacket, AVPacketDeleter > packet( av_packet_alloc() );
	const std::unique_ptr< AVFrame, AVFrameDeleter > frame( av_frame_alloc() );

	if ( !packet || !frame )
	{
		return std::unexpected( idhan::ModuleError( "Failed to allocate packet or frame" ) );
	}

	bool frame_decoded = false;

	while ( av_read_frame( format_context.get(), packet.get() ) >= 0 )
	{
		if ( packet->stream_index == video_stream_index )
		{
			if ( avcodec_send_packet( codec_context.get(), packet.get() ) >= 0 )
			{
				if ( avcodec_receive_frame( codec_context.get(), frame.get() ) >= 0 )
				{
					frame_decoded = true;
					av_packet_unref( packet.get() );
					break;
				}
			}
		}
		av_packet_unref( packet.get() );
	}

	if ( !frame_decoded )
	{
		return std::unexpected( idhan::ModuleError( "Failed to decode frame" ) );
	}

	// Convert frame to RGB24 using swscale
	std::unique_ptr< SwsContext, SwsContextDeleter > sws_context( sws_getContext(
		codec_context->width,
		codec_context->height,
		codec_context->pix_fmt,
		codec_context->width,
		codec_context->height,
		AV_PIX_FMT_RGB24,
		SWS_BILINEAR,
		nullptr,
		nullptr,
		nullptr ) );

	if ( !sws_context )
	{
		return std::unexpected( idhan::ModuleError( "Failed to create swscale context" ) );
	}

	std::unique_ptr< AVFrame, AVFrameDeleter > rgb_frame( av_frame_alloc() );
	if ( !rgb_frame )
	{
		return std::unexpected( idhan::ModuleError( "Failed to allocate RGB frame" ) );
	}

	rgb_frame->format = AV_PIX_FMT_RGB24;
	rgb_frame->width = codec_context->width;
	rgb_frame->height = codec_context->height;

	if ( av_frame_get_buffer( rgb_frame.get(), 0 ) < 0 )
	{
		return std::unexpected( idhan::ModuleError( "Failed to allocate RGB frame buffer" ) );
	}

	sws_scale(
		sws_context.get(),
		frame->data,
		frame->linesize,
		0,
		codec_context->height,
		rgb_frame->data,
		rgb_frame->linesize );

	// Create packed buffer without alignment padding
	const size_t packed_line_size = rgb_frame->width * 3;
	const size_t packed_size = packed_line_size * rgb_frame->height;
	std::vector< unsigned char > packed_data( packed_size );

	// Copy data line by line to remove padding
	for ( int y = 0; y < rgb_frame->height; ++y )
	{
		std::memcpy(
			packed_data.data() + ( y * packed_line_size ),
			rgb_frame->data[ 0 ] + ( y * rgb_frame->linesize[ 0 ] ),
			packed_line_size );
	}

	idhan::VipsImagePtr image { vips_image_new_from_memory(
		packed_data.data(), packed_size, rgb_frame->width, rgb_frame->height, 3, VIPS_FORMAT_UCHAR ) };
	if ( !image ) return std::unexpected( idhan::ModuleError( "Failed to create image from frame" ) );

	const float source_aspect { static_cast< float >( rgb_frame->width ) / static_cast< float >( rgb_frame->height ) };
	const float target_aspect { static_cast< float >( width ) / static_cast< float >( height ) };

	if ( target_aspect > source_aspect )
		width = static_cast< std::size_t >( static_cast< float >( height ) * source_aspect );
	else
		height = static_cast< std::size_t >( static_cast< float >( width ) / source_aspect );

	VipsImage* resized_raw { nullptr };
	if ( vips_resize(
			 image.get(),
			 &resized_raw,
			 static_cast< double >( width ) / static_cast< double >( vips_image_get_width( image.get() ) ),
			 nullptr )
	     != 0 )
		return std::unexpected( idhan::ModuleError( "Failed to resize frame image" ) );
	idhan::VipsImagePtr resized { resized_raw };

	// write_to_memory (in ThumbnailInfo) computes the pipeline here, while packed_data -- which
	// vips_image_new_from_memory references without copying -- is still alive on the stack.
	return idhan::ThumbnailInfo { std::move( resized ) };
}
