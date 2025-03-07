//
// Created by kj16609 on 11/2/24.
//

#pragma once
#include <cstdint>
#include <functional>
#include <string_view>

#include "logging/qt_formatters/qstring.hpp"
#include "spdlog/spdlog.h"

class QNetworkReply;

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
void notify( std::string fmt, Ts&&... ts )
{
	//TODO: Get this name from the context
	spdlog::get( "client" )->info( fmt, std::forward< Ts >( ts )... );
}

template < typename... Ts >
void debug( std::string fmt, Ts&&... ts )
{
	spdlog::get( "client" )->debug( fmt, std::forward< Ts >( ts )... );
}

template < typename... Ts >
void info( std::string fmt, Ts&&... ts )
{
	spdlog::get( "client" )->info( fmt, std::forward< Ts >( ts )... );
}

template < typename... Ts >
void warn( std::string fmt, Ts&&... ts )
{
	spdlog::get( "client" )->warn( fmt, std::forward< Ts >( ts )... );
}

template < typename... Ts >
void error( std::string fmt, Ts&&... ts )
{
	spdlog::get( "client" )->error( fmt, std::forward< Ts >( ts )... );
}

template < typename... Ts >
void critical( std::string fmt, Ts&&... ts )
{
	spdlog::get( "client" )->critical( fmt, std::forward< Ts >( ts )... );
}

/**
 * @brief Logs the network error response to the local log only.
 */
void logResponse( QNetworkReply* reply );

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