//
// Created by kj16609 on 11/25/25.
//
#include "ArchiveThumbnailer.hpp"

#include <json/reader.h>
#include <json/value.h>
#include <vips/vips.h>

#include <filesystem>

#include "archives.hpp"
#include "crypto/simpleHasher.hpp"
#include "spdlog/spdlog.h"
#include "vips.hpp"

namespace
{
// Guards against archive-in-archive recursion: createThumbnail calls back into the generate and
// thumbnail callbacks, which re-dispatch by MIME and can re-enter this thumbnailer for a nested
// archive. Those callbacks run synchronously on the calling thread, so a thread_local depth
// counter bounds the recursion and stops an archive bomb from exhausting the stack.
thread_local std::size_t g_archive_thumbnail_depth { 0 };
constexpr std::size_t MAX_ARCHIVE_THUMBNAIL_DEPTH { 4 };

struct ArchiveThumbnailDepthGuard
{
	ArchiveThumbnailDepthGuard() { ++g_archive_thumbnail_depth; }

	~ArchiveThumbnailDepthGuard() { --g_archive_thumbnail_depth; }
};
} // namespace

std::vector< std::string_view > ArchiveThumbnailer::handleableMimes()
{
	return getHandleableMimesForArchives();
}

std::expected< idhan::ThumbnailInfo, idhan::ModuleError > ArchiveThumbnailer::createThumbnailRaw(
	idhan::ModuleCallData& data,
	std::size_t width,
	std::size_t height )
{
	const ArchiveThumbnailDepthGuard depth_guard {};
	if ( g_archive_thumbnail_depth > MAX_ARCHIVE_THUMBNAIL_DEPTH )
		return std::unexpected( idhan::ModuleError { "Archive nesting too deep for thumbnailing" } );

	const auto& [ file_view, mime, extra ] = data;

	spdlog::trace( "Archive thumbnailer extra json: {}", extra.toStyledString() );

	if ( extra.isNull() )
	{
		return std::unexpected( idhan::ModuleError { "Expected an extra json for archive thumbnailer" } );
	}

	auto members { extra.getMemberNames() };

	if ( !extra.isMember( "encrypted" ) )
	{
		return std::unexpected( idhan::ModuleError { "Generator missing extra info: encrypted flag" } );
	}

	const bool is_encrypted { extra[ "encrypted" ].asBool() };

	if ( is_encrypted )
	{
		const auto svg_data_sized { format_ns::format(
			R"(<?xml version="1.0" ?><svg width="{}px" height="{}px" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><title/><g id="Complete"><g id="lock"><g><rect fill="none" height="10" rx="2" ry="2" stroke="#FFFFFF" stroke-linecap="round" stroke-linejoin="round" stroke-width="2" width="16" x="4" y="11"/><path d="M16.5,11V8h0c0-2.8-.5-5-4.5-5S7.5,5.2,7.5,8h0v3" fill="none" stroke="#FFFFFF" stroke-linecap="round" stroke-linejoin="round" stroke-width="2"/></g></g></g></svg>)",
			width,
			height ) };

		// The C loader takes the raw buffer directly -- no VipsBlob to allocate and hand-free.
		VipsImage* svg_raw { nullptr };
		if ( vips_svgload_buffer(
				 const_cast< void* >( static_cast< const void* >( svg_data_sized.data() ) ),
				 svg_data_sized.size(),
				 &svg_raw,
				 nullptr )
		     != 0 )
			return std::unexpected( idhan::ModuleError { "Failed to load SVG placeholder" } );
		idhan::VipsImagePtr image { svg_raw };

		if ( vips_image_get_bands( image.get() ) > 3 )
		{
			VipsImage* flat_raw { nullptr };
			if ( vips_flatten( image.get(), &flat_raw, nullptr ) != 0 )
				return std::unexpected( idhan::ModuleError { "Failed to flatten SVG placeholder" } );
			image.reset( flat_raw );
		}

		return idhan::ThumbnailInfo { std::move( image ), idhan::ThumbnailInfo::NOCACHE };
	}

	if ( members.size() <= 1 ) // we will always expect 'encrypted' to be a member here as well
		return std::unexpected( idhan::ModuleError { "Zero members for archive. Unable to create thumbnail" } );

	const std::size_t child_thumbnails {
		!members.empty() ? members.size() - 1 : members.size()
	}; // subtract the 'encrypted' member

	// determine a grid; ceil(sqrt) so the canvas is large enough for every child. floor() left
	// non-perfect-square counts with an undersized canvas (relying on insert's expand to paper
	// over it) and collapsed 2-3 child archives to grid_size 1, discarding all but the first.
	auto grid_size {
		static_cast< std::size_t >( std::ceil( std::sqrt( static_cast< double >( child_thumbnails ) ) ) )
	};

	constexpr auto thumb_width { 128 };
	constexpr auto thumb_height { 128 };

	const int grid_width { thumb_width * static_cast< int >( grid_size ) };
	const int grid_height { thumb_height * static_cast< int >( grid_size ) };

	VipsImage* canvas_raw { nullptr };
	if ( vips_black( &canvas_raw, grid_width, grid_height, "bands", 3, nullptr ) != 0 )
		return std::unexpected( idhan::ModuleError { "Failed to create archive thumbnail canvas" } );
	idhan::VipsImagePtr canvas { canvas_raw };

	bool flag_cache_thumbnail { true };
	std::size_t counter { 0 };
	bool all_generate_failed { true };
	std::optional< idhan::ModuleError > last_error { std::nullopt };

	for ( const auto& member : members )
	{
		if ( member.size() != ( 256 / 8 ) * 2 )
		{
			// fixes trying to use members that are not hashes
			continue;
		}

		// member should be a hex string of a sha256
		const auto hash { idhan::crypto::fromHex( member ) };
		const auto file_path_str { extra[ member ].asString() };
		const std::filesystem::path path { file_path_str };

		const auto generated_file {
			this->m_callbacks.generate( file_view, hash, data.extra, path.filename().string() )
		};

		if ( !generated_file )
		{
			// Skip this child rather than failing the whole archive thumbnail; only if every child
			// fails do we surface an error (see the all_generate_failed check below).
			spdlog::warn( "Archive thumbnailer: failed to generate '{}': {}", member, generated_file.error() );
			last_error = generated_file.error();
			flag_cache_thumbnail = false;
			continue;
		}

		all_generate_failed = false;

		const auto thumbnail_rgb { this->m_callbacks.thumbnail( *generated_file, {}, path.filename().string() ) };

		if ( !thumbnail_rgb )
		{
			// Do not cache the thumbnail if we failed to thumbnail whatever was generated
			flag_cache_thumbnail = false;
			counter += 1;
			continue;
		}

		// if the grid size is 1 just return the image
		if ( grid_size == 1 ) return thumbnail_rgb;

		// figure out where in the grid we should put this thumbnail
		const auto x { counter % grid_size };
		const auto y { counter / grid_size };
		counter += 1;
		const auto& [ rgb, gen_thumb_width, gen_thumb_height, cache_thumbnail ] = *thumbnail_rgb;

		if ( !cache_thumbnail ) flag_cache_thumbnail = false;

		VipsImage* thumb_raw { vips_image_new_from_memory_copy(
			rgb.data(),
			rgb.size(),
			static_cast< int >( gen_thumb_width ),
			static_cast< int >( gen_thumb_height ),
			3,
			VIPS_FORMAT_UCHAR ) };
		if ( !thumb_raw ) return std::unexpected( idhan::ModuleError { "Failed to wrap child thumbnail pixels" } );
		idhan::VipsImagePtr thumb { thumb_raw };

		//TODO: Center image

		const int generated_width { vips_image_get_width( thumb.get() ) };
		const int offset_width { ( thumb_width - generated_width ) / 2 };
		const int generated_height { vips_image_get_height( thumb.get() ) };
		const int offset_height { ( thumb_height - generated_height ) / 2 };

		VipsImage* new_canvas_raw { nullptr };
		if ( vips_insert(
				 canvas.get(),
				 thumb.get(),
				 &new_canvas_raw,
				 static_cast< int >( x ) * thumb_width + offset_width,
				 static_cast< int >( y ) * thumb_height + offset_height,
				 "expand",
				 TRUE,
				 nullptr )
		     != 0 )
			return std::unexpected( idhan::ModuleError { "Failed to composite archive thumbnail" } );
		canvas.reset( new_canvas_raw );
	}

	if ( all_generate_failed )
	{
		// last_error may be empty if no member was a valid hash (nothing to generate); never
		// dereference a disengaged optional here.
		return std::unexpected(
			last_error.value_or( idhan::ModuleError { "No thumbnailable entries found in archive" } ) );
	}

	return idhan::ThumbnailInfo { std::move( canvas ), flag_cache_thumbnail };
}