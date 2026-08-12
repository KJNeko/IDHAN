#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#include <spdlog/spdlog.h>
#include <vips/vips.h>
#pragma GCC diagnostic pop

#include <format>

//! Shared libvips bring-up for the premade module libraries.
/** Every premade library ends up touching vips -- the image modules obviously, but also the PSD and
 *  archive thumbnailers (compositing) and the FFmpeg one (ThumbnailerModuleI::createThumbnailFile
 *  encodes through vips). Each library is dlopened into its own worker process, so each one has to
 *  run VIPS_INIT itself; nothing else in that process will have done it.
 *
 *  Header-only and inline so the four libraries can share it without an extra CMake target. */
namespace idhan::premade
{

//! Routes libvips' GLib log domain into spdlog so module output lands in the server's log.
inline void vipsLogHandler(
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

//! Takes over the vips logger and initialises libvips for this process.
/** \param name The vips "program name", used in vips' own diagnostics. Pass the library name so a
 *              crash report names the worker that produced it.
 *
 *  The vips operation cache is disabled outright: a worker's job is one file at a time, so the
 *  cache only grows RSS -- which is the metric the host uses to decide when to retire the worker. */
inline void vipsInit( const char* const name )
{
	constexpr auto VIPS_LOG_DOMAIN { "VIPS" };
	g_log_set_handler( VIPS_LOG_DOMAIN, G_LOG_LEVEL_MASK, &vipsLogHandler, nullptr );
	g_logv( VIPS_LOG_DOMAIN, G_LOG_LEVEL_INFO, "VIPS Logger taken over", nullptr );

	if ( vips_init( name ) != 0 )
	{
		// The host treats a worker that dies during init as "library failed to load" and skips it,
		// so failing loudly here is better than limping on with a half-initialised vips.
		spdlog::critical( "VIPS: vips_init failed: {}", vips_error_buffer() );
		vips_error_clear();
		return;
	}

	vips_leak_set( TRUE );
	vips_cache_set_max( 0 );
	vips_cache_set_max_mem( 0 );
}

//! Tears libvips down. Called from a library's deinit().
inline void vipsShutdown()
{
	vips_shutdown();
}

} // namespace idhan::premade
