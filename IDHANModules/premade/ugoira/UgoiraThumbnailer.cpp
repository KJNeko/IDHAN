#include "UgoiraThumbnailer.hpp"

#include <json/value.h>
#include <vips/vips.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "AnimationManifest.hpp"
#include "MimeIDs.hpp"
#include "imageExtensions.hpp"
#include "crypto/simpleHasher.hpp"
#include "logging/format_ns.hpp"
#include "spdlog/spdlog.h"
#include "vips.hpp"

// ArchiveMimeParser also specializes a manifest-less zip whose root is a numbered image sequence, so
// this is the preferred source of frame order, not a guaranteed one.
static constexpr std::string_view MANIFEST_NAME { "animation.json" };

//! Maps each member path in the archive metadata json to its SHA-256, which is how the generate
//! callback addresses a member.
static std::unordered_map< std::string, std::string > memberHashesByPath( const Json::Value& extra )
{
	std::unordered_map< std::string, std::string > by_path {};

	for ( const auto& key : extra.getMemberNames() )
	{
		if ( key.size() != ( 256 / 8 ) * 2 ) continue;

		const auto& path { extra[ key ] };
		if ( !path.isString() ) continue;

		by_path.emplace( path.asString(), key );
	}

	return by_path;
}

// Frame order for a Ugoira with no manifest: its image members by name, which for the numbered
// sequence ArchiveMimeParser matched is the playback order. Hydrus sorts the same way and shows each
// frame for UGOIRA_DEFAULT_FRAME_DURATION_MS, having no delay to read either.
static std::vector< UgoiraFrame > framesFromMembers( const std::unordered_map< std::string, std::string >& by_path )
{
	std::vector< std::string > paths {};

	for ( const auto& [ path, hash ] : by_path )
		if ( idhan::premade::hasFrameExtension( path ) ) paths.emplace_back( path );

	std::ranges::sort( paths );

	std::vector< UgoiraFrame > frames {};
	frames.reserve( paths.size() );

	for ( auto& path : paths ) frames.emplace_back( std::move( path ), UGOIRA_DEFAULT_FRAME_DELAY_MS );

	return frames;
}

//! Only the manifest goes through this; frames are decoded straight off a VipsModuleSource.
static std::expected< std::vector< std::byte >, idhan::ModuleError > readWhole( const idhan::ModuleFile& file )
{
	std::vector< std::byte > bytes {};
	bytes.resize( file.size() );

	std::size_t offset { 0 };
	while ( offset < bytes.size() )
	{
		const auto count { file.read( std::span< std::byte > { bytes }.subspan( offset ), offset ) };
		if ( !count ) return std::unexpected( count.error() );
		if ( *count == 0 ) return std::unexpected( idhan::ModuleError { "short read on a module file" } );

		offset += *count;
	}

	return bytes;
}

std::vector< idhan::MimeID > UgoiraThumbnailer::handleableMimes()
{
	return { idhan::mime_ids::PIXIV_UGOIRA };
}

std::expected< idhan::ThumbnailInfo, idhan::ModuleError > UgoiraThumbnailer::createThumbnailRaw(
	idhan::ModuleCallData& data,
	const std::size_t width,
	const std::size_t height )
{
	const auto& extra { data.extra };

	if ( !extra.isObject() )
		return std::unexpected( idhan::ModuleError { "Expected the archive metadata json for a Ugoira" } );

	if ( extra.isMember( "encrypted" ) && extra[ "encrypted" ].asBool() )
		return std::unexpected( idhan::ModuleError { "Ugoira is encrypted; its frames cannot be read" } );

	const auto by_path { memberHashesByPath( extra ) };

	std::string manifest_hash {};
	for ( const auto& [ path, hash ] : by_path )
	{
		if ( std::filesystem::path { path }.filename().string() == MANIFEST_NAME )
		{
			manifest_hash = hash;
			break;
		}
	}

	std::vector< UgoiraFrame > frames {};

	if ( manifest_hash.empty() )
	{
		frames = framesFromMembers( by_path );

		if ( frames.empty() )
			return std::unexpected(
				idhan::ModuleError { "Ugoira carries neither an animation.json nor any image member" } );

		spdlog::debug(
			"Ugoira carries no animation.json, showing its {} members at {}ms each",
			frames.size(),
			UGOIRA_DEFAULT_FRAME_DELAY_MS );
	}
	else
	{
		const auto manifest_file { this->m_callbacks.generate(
			data.file, idhan::crypto::fromHex( manifest_hash ), extra, std::string { MANIFEST_NAME } ) };
		if ( !manifest_file ) return std::unexpected( manifest_file.error() );

		const auto manifest_bytes { readWhole( **manifest_file ) };
		if ( !manifest_bytes ) return std::unexpected( manifest_bytes.error() );

		auto parsed { parseAnimationManifest( *manifest_bytes ) };
		if ( !parsed ) return std::unexpected( parsed.error() );

		frames = std::move( *parsed );
	}

	std::vector< idhan::VipsImagePtr > pages {};
	std::vector< int > delays {};
	pages.reserve( frames.size() );
	delays.reserve( frames.size() );

	for ( const auto& frame : frames )
	{
		const auto found { by_path.find( frame.m_file ) };
		if ( found == by_path.end() )
		{
			spdlog::warn( "Ugoira manifest names \'{}\', which the archive does not contain", frame.m_file );
			continue;
		}

		const auto frame_name { std::filesystem::path { frame.m_file }.filename().string() };

		auto frame_file {
			this->m_callbacks.generate( data.file, idhan::crypto::fromHex( found->second ), extra, frame_name )
		};
		if ( !frame_file )
		{
			spdlog::warn( "Ugoira frame \'{}\' could not be extracted: {}", frame.m_file, frame_file.error() );
			continue;
		}

		VipsModuleSource source { **frame_file };
		if ( !source.valid() ) return std::unexpected( idhan::ModuleError { "Failed to open a vips source" } );

		VipsImage* loaded_raw { vips_image_new_from_source( source.get(), "", nullptr ) };
		if ( !loaded_raw )
		{
			spdlog::warn( "Ugoira frame \'{}\' could not be decoded: {}", frame.m_file, vips_error_buffer() );
			vips_error_clear();
			continue;
		}
		idhan::VipsImagePtr loaded { loaded_raw };

		VipsImage* scaled_raw { nullptr };
		if ( vips_thumbnail_image(
				 loaded.get(),
				 &scaled_raw,
				 static_cast< int >( width ),
				 "height",
				 static_cast< int >( height ),
				 nullptr )
		     != 0 )
			return std::unexpected( idhan::ModuleError { "Failed to scale a Ugoira frame" } );
		idhan::VipsImagePtr scaled { scaled_raw };

		// Every page of the join has to agree on band count, and an animation needs no alpha.
		if ( vips_image_get_bands( scaled.get() ) > 3 )
		{
			VipsImage* flat_raw { nullptr };
			if ( vips_flatten( scaled.get(), &flat_raw, nullptr ) != 0 )
				return std::unexpected( idhan::ModuleError { "Failed to flatten a Ugoira frame" } );
			scaled.reset( flat_raw );
		}

		// vips loaders are lazy and this frame's source dies at the end of the iteration, so the page
		// has to be real before then.
		VipsImage* materialised_raw { vips_image_copy_memory( scaled.get() ) };
		if ( !materialised_raw )
			return std::unexpected( idhan::ModuleError { "Failed to materialise a Ugoira frame" } );
		scaled.reset( materialised_raw );

		pages.emplace_back( std::move( scaled ) );
		delays.emplace_back( frame.m_delay_ms );
	}

	if ( pages.empty() ) return std::unexpected( idhan::ModuleError { "No Ugoira frame could be decoded" } );

	std::vector< VipsImage* > page_ptrs {};
	page_ptrs.reserve( pages.size() );
	for ( const auto& page : pages ) page_ptrs.push_back( page.get() );

	const int page_width { vips_image_get_width( page_ptrs.front() ) };
	const int page_height { vips_image_get_height( page_ptrs.front() ) };

	VipsImage* roll_raw { nullptr };
	if ( vips_arrayjoin( page_ptrs.data(), &roll_raw, static_cast< int >( page_ptrs.size() ), "across", 1, nullptr )
	     != 0 )
		return std::unexpected( idhan::ModuleError { "Failed to join Ugoira frames" } );
	idhan::VipsImagePtr roll { roll_raw };

	// vips reads an animation off a single tall image plus this metadata: one page per frame.
	vips_image_set_int( roll.get(), "page-height", page_height );
	vips_image_set_array_int( roll.get(), "delay", delays.data(), static_cast< int >( delays.size() ) );
	vips_image_set_int( roll.get(), "loop", 0 );

	void* buffer { nullptr };
	std::size_t buffer_size { 0 };
	if ( vips_webpsave_buffer( roll.get(), &buffer, &buffer_size, nullptr ) != 0 )
		return std::unexpected(
			idhan::ModuleError {
				format_ns::format( "Failed to encode the Ugoira animation: {}", vips_error_buffer() ) } );

	idhan::ThumbnailInfo info {};
	info.m_pixel_data = std::vector< std::byte > {
		static_cast< std::byte* >( buffer ), static_cast< std::byte* >( buffer ) + buffer_size
	};
	g_free( buffer );

	info.width = static_cast< std::size_t >( page_width );
	info.height = static_cast< std::size_t >( page_height );
	info.m_format = idhan::ThumbnailFormat::ANIMATED;

	return info;
}
