//
// Created by kj16609 on 7/23/24.
//

#pragma once

#include <spdlog/spdlog.h>

#include <string>

namespace idhan::log
{

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