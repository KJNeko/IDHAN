//
// Created by kj16609 on 6/11/25.
//

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#include <spdlog/spdlog.h>
#include <vips/vips.h>
#pragma GCC diagnostic pop

#include <format>
#include <memory>
#include <vector>

#include "ImageVipsMetadata.hpp"
#include "archives/ArchiveGenerator.hpp"
#include "archives/ArchiveMetadata.hpp"
#include "archives/ArchiveThumbnailer.hpp"
#include "ffmpeg/FFMPEGMetadata.hpp"
#include "ffmpeg/FFMPEGThumbnailer.hpp"
#include "psd/PsdMetadata.hpp"
#include "psd/PsdThumbnailer.hpp"
#include "thumbnailers/ImageVipsThumbnailer.hpp"

using namespace idhan;

std::vector< std::shared_ptr< IDHANModule > > getModules( ModuleCallbacks callbacks )
{
	std::vector< std::shared_ptr< IDHANModule > > ret {
		std::make_shared< ImageVipsMetadata >( callbacks ),  std::make_shared< ImageVipsThumbnailer >( callbacks ),
		std::make_shared< PsdMetadata >( callbacks ),        std::make_shared< PsdThumbnailer >( callbacks ),
		std::make_shared< FFMPEGMetadata >( callbacks ),     std::make_shared< FFMPEGThumbnailer >( callbacks ),
		std::make_shared< ArchiveMetadata >( callbacks ),    std::make_shared< ArchiveGenerator >( callbacks ),
		std::make_shared< ArchiveThumbnailer >( callbacks ),
	};

	return ret;
}

void gLoghandler(
	[[maybe_unused]] const char* log_domain,
	const GLogLevelFlags log_level,
	const char* message,
	[[maybe_unused]] gpointer user_data )
{
	switch ( log_level )
	{
		case G_LOG_LEVEL_MASK:
			[[fallthrough]];
		default:
			[[fallthrough]];
		case G_LOG_FLAG_RECURSION:
			[[fallthrough]];
		case G_LOG_FLAG_FATAL:
			[[fallthrough]];
		case G_LOG_LEVEL_CRITICAL:
			spdlog::critical( std::format( "VIPS: {}", message ) );
			break;
		case G_LOG_LEVEL_ERROR:
			spdlog::error( std::format( "VIPS: {}", message ) );
			break;
		case G_LOG_LEVEL_WARNING:
			spdlog::warn( std::format( "VIPS: {}", message ) );
			break;
		case G_LOG_LEVEL_INFO:
			[[fallthrough]];
		case G_LOG_LEVEL_MESSAGE:
			// Very noisy
			// spdlog::info( std::format( "VIPS: {}", message ) );
			break;
		case G_LOG_LEVEL_DEBUG:
			spdlog::debug( std::format( "VIPS: {}", message ) );
	}
}

extern "C" {

void* getModulesFunc()
{
	return reinterpret_cast< void* >( &getModules );
}

// Defined and implemented if a library used by these modules must be initalized first
void init()
{
	// Take over the libVIPS logger
	constexpr auto VIPS_LOG_DOMAIN { "VIPS" };
	g_log_set_handler( VIPS_LOG_DOMAIN, G_LOG_LEVEL_MASK, &gLoghandler, nullptr );
	g_logv( VIPS_LOG_DOMAIN, G_LOG_LEVEL_INFO, "VIPS Logger taken over", nullptr );
	VIPS_INIT( "IDHANPremadeModules" );
	vips_leak_set( TRUE );
	vips_cache_set_max( 0 );
	vips_cache_set_max_mem( 0 );
}

// Defined and implemented if a library used by these modules must be initalized first
void deinit()
{
	vips_shutdown();
}
}
