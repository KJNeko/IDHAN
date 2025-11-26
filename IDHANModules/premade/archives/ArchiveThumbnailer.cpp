//
// Created by kj16609 on 11/25/25.
//
#include "ArchiveThumbnailer.hpp"

#include <json/reader.h>
#include <json/value.h>
#include <vips/vips8>

#include <filesystem>
#include <iostream>

#include "archives.hpp"
#include "crypto/simpleHasher.hpp"

std::vector< std::string_view > ArchiveThumbnailer::handleableMimes()
{
	return getHandleableMimesForArchives();
}

std::expected< idhan::ThumbnailInfo, idhan::ModuleError > ArchiveThumbnailer::createThumbnail(
	idhan::ModuleCallData& data,
	std::size_t width,
	std::size_t height )
{
	const auto [ file_view, mime, extra ] = data;

	std::cout << "Json: " << extra.toStyledString() << std::endl;

	const auto members { extra.getMemberNames() };

	if ( members.size() == 0 )
		return std::unexpected( idhan::ModuleError { "Zero members for archive. Unable to create thumbnail" } );

	// determine a grid
	std::size_t grid_size { 1 };
	std::size_t grid_width { 1 };
	while ( grid_size < members.size() )
	{
		grid_width += 1;
		grid_size = std::pow( grid_width, 2 );
	}

	std::cout << "Grid size: " << grid_size << std::endl;

	constexpr auto thumb_width { 128 };
	constexpr auto thumb_height { 128 };

	vips::VImage canvas { vips::VImage::black(
		thumb_width * grid_width, thumb_height * grid_width, vips::VImage::option()->set( "bands", 3 ) ) };

	std::size_t counter { 0 };
	for ( const auto& member : members )
	{
		// member should be a hex string of a sha256
		const auto hash { idhan::crypto::fromHex( member ) };
		const auto file_path_str { extra[ member ].asString() };
		const std::filesystem::path path { file_path_str };

		const auto generated_file {
			this->m_callbacks.generate( file_view, hash, data.extra, path.filename().string() )
		};
		if ( !generated_file ) return std::unexpected( generated_file.error() );

		const auto thumbnail_rgb { this->m_callbacks.thumbnail( *generated_file, {}, path.filename().string() ) };

		if ( !thumbnail_rgb )
		{
			counter += 1;
			continue;
		}

		// figure out where in the grid we should put this thumbnail
		const auto x { counter % grid_width };
		const auto y { counter / grid_width };
		counter += 1;
		const auto& image_rgb { thumbnail_rgb->data };
		const auto& [ rgb, gen_thumb_width, gen_thumb_height, _ ] = *thumbnail_rgb;

		vips::VImage thumb { vips::VImage::new_from_memory_copy(
			const_cast< void* >( static_cast< const void* >( rgb.data() ) ),
			rgb.size(),
			gen_thumb_width,
			gen_thumb_height,
			3,
			VIPS_FORMAT_UCHAR ) };

		//TODO: Center image
		canvas =
			canvas.insert( thumb, x * thumb_width, y * thumb_height, vips::VImage::option()->set( "expand", true ) );
	}

	idhan::ThumbnailInfo info { canvas };

	return info;
}