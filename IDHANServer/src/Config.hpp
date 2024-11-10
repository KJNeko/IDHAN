//
// Created by kj16609 on 11/8/24.
//
#pragma once

#include <toml++/toml.hpp>

#include <filesystem>

namespace idhan::config
{
toml::parse_result loadConfig();
void saveConfig( const toml::parse_result& config );

template < typename T >
static std::optional< T > get( std::string_view group, std::string_view name )
{
	const auto config { loadConfig() };

	return config[ group ][ name ].value< T >();
}

template < typename T >
static T get( std::string_view group, std::string_view name, const T default_value )
{
	const auto config { loadConfig() };

	return config[ group ][ name ].value_or< T >( default_value );
}

template < typename T >
static void set( T& value, const std::string_view group, std::string_view name )
{
	const auto config { loadConfig() };

	config[ group ][ name ] = value;

	saveConfig( config );
}

} // namespace idhan::config