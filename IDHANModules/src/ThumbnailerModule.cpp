//
// Created by kj16609 on 6/11/25.
//

#include "ThumbnailerModule.hpp"

#include <vips/vips8>

#include "ThumbnailInfo.hpp"

namespace idhan
{

ThumbnailInfo::ThumbnailInfo( vips::VImage& image, const bool do_cache_thumbnail ) :
  width( image.width() ),
  height( image.height() ),
  cache_thumbnail( do_cache_thumbnail ),
  m_mode( ThumbnailInfo::RAW )
{
	std::size_t output_length { 0 };
	auto image_data { image.write_to_memory( &output_length ) };

	this->data.resize( output_length );
	std::memcpy( this->data.data(), image_data, output_length );

	// write_to_memory returns a g_malloc'd buffer owned by the caller
	g_free( image_data );
}

ThumbnailerModuleI::~ThumbnailerModuleI() = default;

std::expected< ThumbnailInfo, ModuleError > ThumbnailerModuleI::createThumbnailFile(
	ModuleCallData& data,
	std::size_t width,
	std::size_t height )
{
	const auto thumbnail { createThumbnail( data, width, height ) };
	if ( !thumbnail ) return std::unexpected( thumbnail.error() );

	const auto [ thumbnail_rgb, thumbnail_width, thumbnail_height, cache_thumbnail, _ ] = *thumbnail;

	vips::VImage resized { vips::VImage::new_from_memory_copy(
		const_cast< void* >( static_cast< const void* >( thumbnail_rgb.data() ) ),
		thumbnail_rgb.size(),
		thumbnail_width,
		thumbnail_height,
		3,
		VIPS_FORMAT_UCHAR ) };

	const auto png_data { resized.pngsave_buffer() };

	std::vector< std::byte > output(
		static_cast< std::byte* >( png_data->area.data ),
		static_cast< std::byte* >( png_data->area.data ) + png_data->area.length );

	// pngsave_buffer returns a VipsBlob the caller must unref
	vips_area_unref( VIPS_AREA( png_data ) );

	ThumbnailInfo info {};
	info.data = std::move( output );
	info.width = width;
	info.height = height;
	info.m_mode = ThumbnailInfo::FILE_PNG;
	info.cache_thumbnail = cache_thumbnail;

	return info;
}

bool ThumbnailerModuleI::canHandle( const std::string_view mime )
{
	return std::ranges::any_of(
		handleableMimes(),
		[ &mime ]( const std::string_view handleable_mime ) noexcept -> bool { return mime == handleable_mime; } );
}

ModuleType ThumbnailerModuleI::type()
{
	return ModuleTypeFlags::THUMBNAILER;
}
} // namespace idhan