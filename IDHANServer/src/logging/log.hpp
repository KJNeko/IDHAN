//
// Created by kj16609 on 7/23/24.
//

#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Weffc++"
#include <spdlog/spdlog.h>
#ifdef IDHAN_USE_STD_FORMAT
#include <format>
#else
#include <fmt/format.h>
#endif
#pragma GCC diagnostic pop

#include <string>

#include "qt_formatters/qstring.hpp"

#ifdef IDHAN_USE_STD_FORMAT
namespace format_ns = std;
#else
namespace format_ns = fmt;
#endif

namespace idhan::log
{

template < typename... Ts >
void trace( const format_ns::format_string< Ts... > str, Ts&&... ts )
{
	::spdlog::trace( str, std::forward< Ts >( ts )... );
}

template < typename... Ts >
void debug( const format_ns::format_string< Ts... > str, Ts&&... ts )
{
	::spdlog::debug( str, std::forward< Ts >( ts )... );
}

template < typename T >
void debug( const T& t )
{
	::spdlog::debug( t );
}

template < typename... Ts >
void info( const format_ns::format_string< Ts... > str, Ts&&... ts )
{
	::spdlog::info( str, std::forward< Ts >( ts )... );
}

template < typename T >
void info( const T& t )
{
	::spdlog::info( t );
}

template < typename... Ts >
void warn( const format_ns::format_string< Ts... > str, Ts&&... ts )
{
	::spdlog::warn( str, std::forward< Ts >( ts )... );
}

template < typename T >
void warn( const T& t )
{
	::spdlog::warn( t );
}

template < typename... Ts >
void error( const format_ns::format_string< Ts... > str, Ts&&... ts )
{
	::spdlog::error( str, std::forward< Ts >( ts )... );
}

template < typename T >
void error( const T& val )
{
	::spdlog::error( val );
}

template < typename... Ts >
void critical( const format_ns::format_string< Ts... > str, Ts&&... ts )
{
	::spdlog::critical( str, std::forward< Ts >( ts )... );
}

template < typename T >
void critical( const T& val )
{
	::spdlog::critical( val );
}

namespace server
{
template < typename... Ts >
void info( const format_ns::format_string< Ts... > str, Ts&&... ts )
{
	::spdlog::info( str, std::forward< Ts >( ts )... );
}
} // namespace server

} // namespace idhan::log