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

#include "qt_formatters/qstring.hpp"

namespace idhan::log
{

template < typename... Ts >
void trace( const std::string str, Ts&&... ts )
{
	::spdlog::trace( str, std::forward< Ts >( ts )... );
}

template < typename... Ts >
void debug( const std::string str, Ts&&... ts )
{
	::spdlog::debug( str, std::forward< Ts >( ts )... );
}

template < typename... Ts >
void info( const std::string str, Ts&&... ts )
{
	::spdlog::info( str, std::forward< Ts >( ts )... );
}

template < typename... Ts >
void warn( const std::string str, Ts&&... ts )
{
	::spdlog::warn( str, std::forward< Ts >( ts )... );
}

template < typename... Ts >
void error( const std::string str, Ts&&... ts )
{
	::spdlog::error( str, std::forward< Ts >( ts )... );
}

template < typename... Ts >
void critical( const std::string str, Ts&&... ts )
{
	::spdlog::critical( str, std::forward< Ts >( ts )... );
}

namespace server
{
template < typename... Ts >
void info( const std::string str, Ts&&... ts )
{
	::spdlog::info( std::format( "[SERVER]: {}", str ), std::forward< Ts >( ts )... );
}
} // namespace server

} // namespace idhan::log