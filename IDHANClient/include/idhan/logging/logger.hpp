//
// Created by kj16609 on 11/2/24.
//

#pragma once
#include <cstdint>
#include <functional>
#include <string_view>

/**
 *
 * IDHAN logging functions should be used as a way to transmit logging information back to the server. There are the following levels
 * notify, info, warn, error, critical.
 * It is expected that upon a critical message, the application is about to die.
 *
 * There are also functions to implement callbacks for certian error levels and notifications
 *
 * - notify: used to explicitly notify the user of an event.
 * - info: used to silently notify the user. This should ideally be used for some completion states where notify should be used for a final completion.
 * - warn: used to notify the user of a possible issue
 * - error: used to notify the user of an error
 * - critical: used to notify that your process is about to end, violently or willingly.
 */
namespace idhan::logging
{

template < typename... Ts >
void notify( const std::string_view& fmt, const Ts&... ts )
{}

template < typename... Ts >
void info( const std::string_view& fmt, const Ts&... ts )
{}

template < typename... Ts >
void warn( const std::string_view& fmt, const Ts&... ts )
{}

template < typename... Ts >
void error( const std::string_view& fmt, const Ts&... ts )
{}

template < typename... Ts >
void critical( const std::string_view& fmt, const Ts&... ts )
{}

enum CallbackLevel : uint8_t
{
	Notify = 1 << 0,
	Info = 1 << 1,
	Warn = 1 << 2,
	Error = 1 << 3,
	Critical = 1 << 4,
	All = Notify | Info | Warn | Error | Critical
};

using CallbackFunction = std::function< void( CallbackLevel level, std::string_view message ) >;

//! Registers a callback. The level will act as a mask for specific events
void registerCallback( CallbackFunction& func, CallbackLevel level );

} // namespace idhan::logging

namespace IDHAN::log
{
using namespace idhan::logging;
}