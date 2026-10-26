#pragma once

#include <spdlog/spdlog.h>

#include <exception>
#include <expected>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>

#include "js/ScriptContext.hpp"

namespace idhan::downloader
{
// Host adapters may call databases or user code. Never let their exceptions escape a worker.
template < typename Function >
auto hostCallback( Function&& function ) -> std::expected< std::invoke_result_t< Function >, std::string >
{
	const ScriptContext::HostCall host_call {};

	try
	{
		if constexpr ( std::is_void_v< std::invoke_result_t< Function > > )
		{
			std::invoke( std::forward< Function >( function ) );
			return {};
		}
		else
			return std::invoke( std::forward< Function >( function ) );
	}
	catch ( const std::exception& error )
	{
		spdlog::error( "Downloader host callback failed: {}", error.what() );
		return std::unexpected( std::string { error.what() } );
	}
	catch ( ... )
	{
		spdlog::error( "Downloader host callback failed with an unknown exception" );
		return std::unexpected( "Unknown host callback exception" );
	}
}

// Flatten a fallible host operation while preserving its ordinary error result.
template < typename Function >
auto hostOperation( Function&& function ) -> std::invoke_result_t< Function >
{
	auto result { hostCallback( std::forward< Function >( function ) ) };
	if ( !result ) return std::unexpected( std::move( result.error() ) );

	return std::move( *result );
}
} // namespace idhan::downloader
