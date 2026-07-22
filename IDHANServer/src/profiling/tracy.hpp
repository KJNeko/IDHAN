//
// Tracy profiler wrapper. Include this (never <tracy/Tracy.hpp> directly) so that
// builds without IDHAN_ENABLE_TRACY compile every macro to a no-op.
//
#pragma once

#ifdef TRACY_ENABLE
	#include <tracy/Tracy.hpp>
#else
	// No-op fallbacks — keep #ifdef out of call sites.
	#define ZoneScoped
	#define ZoneScopedN( name )
	#define ZoneScopedNC( name, color )
	#define ZoneText( txt, size )
	#define ZoneName( txt, size )
	#define FrameMark
	#define FrameMarkNamed( name )
	#define TracyFiberEnter( fiber )
	#define TracyFiberLeave
	#define TracyMessageL( msg )
	#define TracyMessage( msg, size )
#endif
