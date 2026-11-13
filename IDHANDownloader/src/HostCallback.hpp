#pragma once

#include <folly/coro/Task.h>
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
//! Host adapters may call databases or user code. Never let their exceptions escape a worker.
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

//! Flatten a fallible host operation while preserving its ordinary error result.
template < typename Function >
auto hostOperation( Function&& function ) -> std::invoke_result_t< Function >
{
	auto result { hostCallback( std::forward< Function >( function ) ) };
	if ( !result ) return std::unexpected( std::move( result.error() ) );

	return std::move( *result );
}

template < typename T >
folly::coro::Task< std::expected< T, std::string > > hostOperation(
	folly::coro::Task< std::expected< T, std::string > > operation )
{
	try
	{
		co_return co_await std::move( operation );
	}
	catch ( const std::exception& error )
	{
		co_return std::unexpected( std::string { error.what() } );
	}
	catch ( ... )
	{
		co_return std::unexpected( "Unknown host callback exception" );
	}
}

inline folly::coro::Task< std::expected< void, std::string > > hostCallback( folly::coro::Task< void > operation )
{
	try
	{
		co_await std::move( operation );
		co_return std::expected< void, std::string > {};
	}
	catch ( const std::exception& error )
	{
		co_return std::unexpected( std::string { error.what() } );
	}
	catch ( ... )
	{
		co_return std::unexpected( "Unknown host callback exception" );
	}
}

inline void reportHostTaskResult( folly::Try< void >&& result )
{
	if ( result.hasException() ) spdlog::error( "Downloader coroutine failed: {}", result.exception().what() );
}

inline folly::coro::Task< void > logHostTaskErrors( folly::coro::Task< void > operation )
{
	const auto completed_e { co_await hostCallback( std::move( operation ) ) };
	if ( !completed_e.has_value() ) spdlog::error( "Downloader coroutine failed: {}", completed_e.error() );
}

} // namespace idhan::downloader
