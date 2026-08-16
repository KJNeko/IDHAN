#pragma once
#include <cstdint>
#include <functional>
#include <string_view>

#include "logging/qt_formatters/qstring.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#include <spdlog/spdlog.h>
#pragma GCC diagnostic pop

#include "logging/format_ns.hpp"

class QNetworkReply;

/**
 *
 * Transmits logging information back to the server. Callbacks can be registered per level; see
 * registerCallback.
 *
 * - notify: explicitly notify the user of an event, such as a final completion.
 * - info: silently notify the user, such as an intermediate completion state.
 * - warn: a possible issue.
 * - error: an error.
 * - critical: the process is about to end, violently or willingly.
 */
namespace idhan::logging
{

//! \return The client's shared spdlog logger (created on first use).
std::shared_ptr< spdlog::logger > getLogger();

//! Logs at "notify" level (see the namespace description for level semantics).
template < typename... Ts >
void notify( format_ns::format_string< Ts... > fmt, Ts&&... ts )
{
	auto logger { getLogger() };
	if ( !logger ) throw std::runtime_error( "Logger not found" );
	logger->info( format_ns::format( fmt, std::forward< Ts >( ts )... ) );
}

template < typename... Ts >
void debug( format_ns::format_string< Ts... > fmt, Ts&&... ts )
{
	auto logger { getLogger() };
	if ( !logger ) throw std::runtime_error( "Logger not found" );
	logger->debug( format_ns::format( fmt, std::forward< Ts >( ts )... ) );
}

template < typename... Ts >
void info( format_ns::format_string< Ts... > fmt, Ts&&... ts )
{
	auto logger { getLogger() };
	if ( !logger ) throw std::runtime_error( "Logger not found" );
	logger->info( format_ns::format( fmt, std::forward< Ts >( ts )... ) );
}

template < typename... Ts >
void warn( format_ns::format_string< Ts... > fmt, Ts&&... ts )
{
	auto logger { getLogger() };
	if ( !logger ) throw std::runtime_error( "Logger not found" );
	logger->warn( format_ns::format( fmt, std::forward< Ts >( ts )... ) );
}

template < typename... Ts >
void error( format_ns::format_string< Ts... > fmt, Ts&&... ts )
{
	auto logger { getLogger() };
	if ( !logger ) throw std::runtime_error( "Logger not found" );
	logger->error( format_ns::format( fmt, std::forward< Ts >( ts )... ) );
}

template < typename T >
void error( const T& msg )
{
	auto logger { getLogger() };
	if ( !logger ) throw std::runtime_error( "Logger not found" );
	logger->error( msg );
}

template < typename... Ts >
void critical( format_ns::format_string< Ts... > fmt, Ts&&... ts )
{
	auto logger { getLogger() };
	if ( !logger ) throw std::runtime_error( "Logger not found" );
	logger->critical( format_ns::format( fmt, std::forward< Ts >( ts )... ) );
}

//! Logs the network error response to the local log only.
void logResponse( QNetworkReply* reply );

//! Bitmask of log levels a callback subscribes to (see registerCallback).
enum CallbackLevel : uint8_t
{
	Notify = 1 << 0,
	Info = 1 << 1,
	Warn = 1 << 2,
	Error = 1 << 3,
	Critical = 1 << 4,
	All = Notify | Info | Warn | Error | Critical
};

//! Callback signature receiving the log level and the formatted message.
using CallbackFunction = std::function< void( CallbackLevel level, std::string_view message ) >;

//! Registers a callback. The level will act as a mask for specific events
void registerCallback( CallbackFunction& func, CallbackLevel level );

} // namespace idhan::logging

namespace IDHAN::log
{
using namespace idhan::logging;
}