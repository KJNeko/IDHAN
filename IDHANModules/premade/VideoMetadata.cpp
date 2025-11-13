//
// Created by kj16609 on 11/12/25.
//
#include "VideoMetadata.hpp"

#include <array>
#include <cstring>
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
	const auto& buffer_view { *static_cast< OpaqueInfo* >( opaque ) };

	auto* data { buffer_view.m_data.data() };
	if ( buffer_view.m_cursor >= buffer_view.m_data.size() ) return 0;

	data += buffer_view.m_cursor;
	const std::int64_t size { static_cast< std::int64_t >( buffer_view.m_data.size() ) - buffer_view.m_cursor };
	const std::int64_t min_size { std::min( size, static_cast< std::int64_t >( buffer_size ) ) };
	std::memcpy( buffer, data, min_size );

	return min_size;
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
	std::array< std::byte, BUFFER_SIZE > buffer {};

	const std::shared_ptr< AVIOContext > avio_context(
		avio_alloc_context(
			reinterpret_cast< unsigned char* >( buffer.data() ),
			buffer.size(),
			0,
			&opaque_info,
			&readFunction,
			nullptr,
			nullptr ),
		&av_free );

	// auto format_context_u { std::unique_ptr< AVFormatContext, void ( * )( AVFormatContext* ) >(
	// ::avformat_alloc_context(), freeAVFormatContext ) };

	// auto format_context { format_context_u.get() };
	const auto format_context_p =
		std::shared_ptr< AVFormatContext >( avformat_alloc_context(), &avformat_free_context );
	auto format_context { format_context_p.get() };

	format_context->pb = avio_context.get();
	format_context->flags |= AVFMT_FLAG_CUSTOM_IO;

	if ( avformat_open_input( &format_context, "dummy file", nullptr, nullptr ) < 0 )
	{
		return std::unexpected( idhan::ModuleError( "Failed to open file" ) );
	}

	base_info.m_metadata = video_metadata;

	return base_info;
}