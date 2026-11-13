#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#include <spdlog/spdlog.h>
#include <vips/vips.h>
#pragma GCC diagnostic pop

#include <format>

#include "renderThreads.hpp"

namespace idhan::premade
{

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
			// Dropped: vips is far too noisy at these levels to be worth logging.
			break;
		case G_LOG_LEVEL_DEBUG:
			spdlog::trace( "VIPS: {}", message );
	}
}

//! Initializes vips with the operation cache disabled: calls rarely repeat an operation, so the
//! cache would only grow RSS, which the host uses to decide when to retire the worker.
//! \param name The vips "program name" used in its diagnostics. Pass the library name so a crash
//!        report names the worker that produced it.
inline void vipsInit( const char* const name )
{
	constexpr auto VIPS_LOG_DOMAIN { "VIPS" };
	g_log_set_handler( VIPS_LOG_DOMAIN, G_LOG_LEVEL_MASK, &vipsLogHandler, nullptr );
	g_log( VIPS_LOG_DOMAIN, G_LOG_LEVEL_INFO, "VIPS Logger taken over" );

	if ( vips_init( name ) != 0 )
	{
		spdlog::critical( "VIPS: vips_init failed: {}", vips_error_buffer() );
		vips_error_clear();
		return;
	}

	vips_cache_set_max( 0 );
	vips_cache_set_max_mem( 0 );

	// vips sizes its pool per operation, so without this a worker running N calls at once asks for
	// N * nproc threads.
	if ( const auto threads { renderThreads() }; threads > 0 ) vips_concurrency_set( static_cast< int >( threads ) );

	spdlog::debug( "VIPS: {} threads per operation", vips_concurrency_get() );
}

inline void vipsShutdown()
{
	vips_shutdown();
}

} // namespace idhan::premade
