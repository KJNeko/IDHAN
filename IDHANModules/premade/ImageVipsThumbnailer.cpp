//
// Created by kj16609 on 6/12/25.
//
#include "ImageVipsThumbnailer.hpp"

#include <vips/vips8>
#include "vips.hpp"

std::vector< std::string_view > ImageVipsThumbnailer::handleableMimes()
{
	return vipsHandleable();
}

std::expected< ThumbnailerModuleI::ThumbnailInfo, ModuleError > ImageVipsThumbnailer::createThumbnail(
	void* data, std::size_t length, std::size_t width, std::size_t height, const std::string mime_name )
{
	VipsImage* image;

	if ( mime_name == "image/png" )
	{
		if ( vips_pngload_buffer( data, length, &image, nullptr ) != 0 )
		{
			return std::unexpected( ModuleError { "Failed to load image" } );
		}
	}
	else if ( mime_name == "image/jpeg" )
	{
		if ( vips_jpegload_buffer( data, length, &image, nullptr ) != 0 )
		{
			return std::unexpected( ModuleError { "Failed to load image" } );
		}
	}
	else if ( mime_name == "image/webp" )
	{
		if ( vips_webpload_buffer( data, length, &image, nullptr ) != 0 )
		{
			return std::unexpected( ModuleError { "Failed to load image" } );
		}
	}
	else
	{
		return std::unexpected( ModuleError { "Invalid mime type" } );
	}

	// { vips_image_new_from_buffer( data, length, nullptr ) };

	if ( !image )
	{
		return std::unexpected( ModuleError { "Failed to load image" } );
	}

	const auto source_width { vips_image_get_width( image ) };
	const auto source_height { vips_image_get_height( image ) };

	const float source_aspect { static_cast< float >( source_width ) / static_cast< float >( source_height ) };
	const float target_aspect { static_cast< float >( width ) / static_cast< float >( height ) };

	if ( target_aspect > source_aspect )
	{
		width = static_cast< int >( height * source_aspect );
	}
	else
	{
		height = static_cast< int >( width / source_aspect );
	}

	VipsImage* resized;
	if ( vips_resize( image, &resized, width / (float)vips_image_get_width( image ), nullptr ) )
	{
		g_object_unref( image );
		return std::unexpected( ModuleError { "Failed to resize image" } );
	}
	g_object_unref( image );

	void* output_buffer;
	size_t output_length;
	if ( vips_pngsave_buffer( resized, &output_buffer, &output_length, nullptr ) )
	{
		g_object_unref( resized );
		return std::unexpected( ModuleError { "Failed to save image" } );
	}
	g_object_unref( resized );

	std::vector< unsigned char > output(
		static_cast< unsigned char* >( output_buffer ),
		static_cast< unsigned char* >( output_buffer ) + output_length );
	g_free( output_buffer );

	ThumbnailerModuleI::ThumbnailInfo ret {};
	ret.data = std::move( output );
	ret.width = width;
	ret.height = height;

	return ret;
}
