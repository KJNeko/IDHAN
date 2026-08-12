#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Weffc++"
#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/spdlog.h>
#pragma GCC diagnostic pop

#include <memory>
#include <string>

#include "logging/format_ns.hpp"
#include "qt_formatters/qstring.hpp"

namespace idhan::log
{

//! The logger IDHAN itself created, and the ring buffer sink attached to it, kept as direct
//! typed pointers set once at startup by ServerContext::createLogger(). Endpoints that need
//! to reach into these (e.g. /log) should use these accessors instead of going through
//! spdlog's global name registry (spdlog::get()/spdlog::default_logger()) or
//! dynamic_pointer_cast to rediscover the sink: modules are dlopen'd with RTLD_GLOBAL and
//! also link spdlog directly, and on some platforms/spdlog builds this has been observed to
//! produce duplicate RTTI for spdlog::sinks::ringbuffer_sink_mt across the executable and the
//! module .so, causing dynamic_pointer_cast to silently return null for the correct object,
//! and/or the registry lookup to return a null or unrelated logger.
[[nodiscard]] std::shared_ptr< spdlog::logger > getServerLogger();
[[nodiscard]] std::shared_ptr< spdlog::sinks::ringbuffer_sink_mt > getServerRingBufferSink();
void setServerLogger(
	std::shared_ptr< spdlog::logger > logger,
	std::shared_ptr< spdlog::sinks::ringbuffer_sink_mt > ring_buffer_sink );

#ifndef IDHAN_DISABLE_TRACE_LOGGING
template < typename... Ts >
void trace( const format_ns::format_string< Ts... > str, Ts&&... ts )
{
	::spdlog::trace( format_ns::format( str, std::forward< Ts >( ts )... ) );
}
#else
template < typename... Ts >
void trace( [[maybe_unused]] const format_ns::format_string< Ts... > str, [[maybe_unused]] Ts&&... ts )
{}
#endif

template < typename... Ts >
void debug( const format_ns::format_string< Ts... > str, Ts&&... ts )
{
	::spdlog::debug( format_ns::format( str, std::forward< Ts >( ts )... ) );
}

template < typename T >
void debug( const T& t )
{
	::spdlog::debug( t );
}

template < typename... Ts >
void info( const format_ns::format_string< Ts... > str, Ts&&... ts )
{
	::spdlog::info( format_ns::format( str, std::forward< Ts >( ts )... ) );
}

template < typename T >
void info( const T& t )
{
	::spdlog::info( t );
}

template < typename... Ts >
void warn( const format_ns::format_string< Ts... > str, Ts&&... ts )
{
	::spdlog::warn( format_ns::format( str, std::forward< Ts >( ts )... ) );
}

template < typename T >
void warn( const T& t )
{
	::spdlog::warn( t );
}

template < typename... Ts >
void error( const format_ns::format_string< Ts... > str, Ts&&... ts )
{
	::spdlog::error( format_ns::format( str, std::forward< Ts >( ts )... ) );
}

template < typename T >
void error( const T& val )
{
	::spdlog::error( val );
}

template < typename... Ts >
void critical( const format_ns::format_string< Ts... > str, Ts&&... ts )
{
	::spdlog::critical( format_ns::format( str, std::forward< Ts >( ts )... ) );
}

template < typename T >
void critical( const T& val )
{
	::spdlog::critical( val );
}

} // namespace idhan::log
