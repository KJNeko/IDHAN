//
// Created by kj16609 on 7/23/24.
//

#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Weffc++"
#include <spdlog/spdlog.h>
#pragma GCC diagnostic pop

#include <string>

#include "logging/format_ns.hpp"
#include "qt_formatters/qstring.hpp"

namespace idhan::log
{

template < typename... Ts >
void trace( const format_ns::format_string< Ts... > str, Ts&&... ts )
{
	::spdlog::trace( format_ns::format( str, std::forward< Ts >( ts )... ) );
}

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