#include "ThumbnailerModule.hpp"

#include <vips/vips.h>

#include <stdexcept>

#include "ThumbnailInfo.hpp"

namespace idhan
{

ThumbnailInfo::ThumbnailInfo( VipsImagePtr&& image, const bool do_cache_thumbnail ) :
  width( static_cast< std::size_t >( vips_image_get_width( image.get() ) ) ),
  height( static_cast< std::size_t >( vips_image_get_height( image.get() ) ) ),
  cache_thumbnail( do_cache_thumbnail )
{
	std::size_t output_length { 0 };
	// vips_image_write_to_memory returns a g_malloc'd buffer owned by the caller (freed with g_object_unref).
	void* image_data { vips_image_write_to_memory( image.get(), &output_length ) };
	if ( !image_data ) throw std::runtime_error( "vips_image_write_to_memory failed" );

	this->m_pixel_data.resize( output_length );
	std::memcpy( this->m_pixel_data.data(), image_data, output_length );

	g_free( image_data );
}

ThumbnailerModuleI::~ThumbnailerModuleI() = default;

std::expected< ThumbnailInfo, ModuleError > ThumbnailerModuleI::createThumbnailFile(
	ModuleCallData& data,
	const std::size_t width,
	const std::size_t height )
{
	auto thumbnail { createThumbnailRaw( data, width, height ) };
	if ( !thumbnail ) return std::unexpected( thumbnail.error() );

	// An animation arrives already encoded; there are no loose pixels to wrap and nothing to re-encode.
	if ( thumbnail->m_format == ThumbnailFormat::ANIMATED ) return thumbnail;

	const auto& [ thumbnail_rgb, thumbnail_width, thumbnail_height, cache_thumbnail, format ] = *thumbnail;

	VipsImage* rgb_image { vips_image_new_from_memory_copy(
		thumbnail_rgb.data(),
		thumbnail_rgb.size(),
		static_cast< int >( thumbnail_width ),
		static_cast< int >( thumbnail_height ),
		3,
		VIPS_FORMAT_UCHAR ) };
	if ( !rgb_image ) return std::unexpected( ModuleError { "Failed to wrap thumbnail pixels" } );

	void* buffer { nullptr };
	std::size_t size { 0 };
	if ( vips_image_write_to_buffer( rgb_image, ".webp", &buffer, &size, nullptr ) != 0 )
	{
		g_object_unref( rgb_image );
		return std::unexpected( ModuleError { "Failed to encode thumbnail to WEBP" } );
	}
	g_object_unref( rgb_image );

	std::vector< std::byte > output( static_cast< std::byte* >( buffer ), static_cast< std::byte* >( buffer ) + size );
	g_free( buffer );

	ThumbnailInfo info {};
	info.m_pixel_data = std::move( output );
	info.width = width;
	info.height = height;
	info.cache_thumbnail = cache_thumbnail;

	return info;
}

bool ThumbnailerModuleI::canHandle( const MimeID mime_id )
{
	return std::ranges::contains( handleableMimes(), mime_id );
}

ModuleType ThumbnailerModuleI::type()
{
	return ModuleTypeFlags::THUMBNAILER;
}
} // namespace idhan