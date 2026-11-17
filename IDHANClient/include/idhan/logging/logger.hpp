#pragma once
#include <cstdint>
#include <functional>
#include <string_view>

#include "idhan/IDHANClientExport.hpp"
#include "logging/qt_formatters/qstring.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#include <spdlog/spdlog.h>
#pragma GCC diagnostic pop

#include "logging/format_ns.hpp"

class QNetworkReply;

namespace idhan::logging
{

IDHAN_CLIENT_EXPORT std::shared_ptr< spdlog::logger > getLogger();

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

IDHAN_CLIENT_EXPORT void logResponse( QNetworkReply* reply );

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

//! TODO: Implement callbacks
IDHAN_CLIENT_EXPORT void registerCallback( CallbackFunction& func, CallbackLevel level );

} // namespace idhan::logging

namespace IDHAN::log
{
using namespace idhan::logging;
}
