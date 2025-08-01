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
static T get( std::string_view group, std::string_view name, const auto default_value )
{
	const auto config { loadConfig() };

	if ( const auto config_value { config[ group ][ name ].value< T >() } ) return config_value.value();
	return default_value;
}

template < typename T >
static void set( const T& value, const std::string_view group, std::string_view name )
{
	const auto config { loadConfig() };

	config[ group ][ name ] = value;

	saveConfig( config );
}

void setLocation( std::filesystem::path path );

} // namespace idhan::config