//
// Created by kj16609 on 11/8/24.
//
#pragma once

#include <toml++/toml.hpp>

#include <filesystem>
#include <ranges>
#include <variant>

#include "logging/log.hpp"

namespace idhan::config
{
using ConfigType = std::variant< std::string, std::size_t >;

//! Locations to search for config info, Searches user path first, then the inverse of this list
#if defined( __linux__ )
constexpr std::array< std::string_view, 4 > config_paths {
	"/usr/share/idhan/config.toml", "/etc/idhan/config.toml", "~/.config/idhan/config.toml", "./config.toml"
};
#elif defined( _WIN32 )
constexpr std::array< std::string_view, 4 > config_paths { "%ProgramData%\\idhan\\config.toml",
	                                                       "%APPDATA%\\idhan\\config.toml",
	                                                       "%LOCALAPPDATA%\\idhan\\config.toml",
	                                                       "./config.toml" };
#endif

std::string_view getUserConfigPath();

template < typename T >
std::optional< T > getValue( std::string_view path, const std::string_view group, const std::string_view name )
{
	if ( !std::filesystem::exists( path ) ) return std::nullopt;

	try
	{
		auto config = toml::parse_file( path.data() );
		if ( auto* table = config[ group ].as_table() )
		{
			if ( const auto value = ( *table )[ name ] )
			{
				if constexpr (
					std::is_same_v< T, std::string > || std::is_same_v< T, bool > || std::is_same_v< T, double >
					|| std::is_same_v< T, std::int64_t > )
				{
					return **value.as< T >();
				}
				else if constexpr ( std::is_integral_v< T > )
				{
					return static_cast< T >( **value.as< std::int64_t >() );
				}
				else
					static_assert( false, "Unsupported toml config type" );
			}
			return std::nullopt;
		}
		return std::nullopt;
	}
	catch ( const toml::parse_error& err )
	{
		log::warn( "Failed to parse config file: {}", err.what() );
	}

	return std::nullopt;
}

template < typename T >
std::optional< T > getValue( const std::string_view group, const std::string_view name )
{
	const auto user_config_path { getUserConfigPath() };
	if ( user_config_path.empty() )
	{
		if ( auto result = getValue< T >( user_config_path, group, name ); result )
		{
			return *result;
		}
	}

	for ( const auto& path : config_paths | std::views::reverse )
	{
		if ( auto result = getValue< T >( path, group, name ); result )
		{
			return *result;
		}
	}

	return std::nullopt;
}

template < typename T >
std::optional< T > get( std::string_view group, std::string_view name )
{
	return getValue< T >( group, name );
}

template < typename T >
T get( std::string_view group, std::string_view name, const auto default_value )
{
	const auto ret { get< T >( group, name ) };

	if ( ret ) return *ret;

	log::warn(
		"Loaded default config from group: \'{}\' name: \'{}\' with default value \'{}\'. You might wanna set this value in a config file",
		group,
		name,
		default_value );

	return default_value;
}

void setLocation( std::filesystem::path path );

} // namespace idhan::config