#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <simdjson.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace idhan::downloader::manifest_json
{
using simdjson::dom::element;

constexpr std::int64_t js_max_safe_integer { ( std::int64_t { 1 } << 53 ) - 1 };

[[noreturn]] inline void invalid( const std::string_view field, const std::string_view reason )
{
	throw std::runtime_error( "Manifest field '" + std::string { field } + "': " + std::string { reason } );
}

inline simdjson::dom::object object( const element value )
{
	simdjson::dom::object result {};
	if ( value.get_object().get( result ) )
	{
		invalid( "object", "expected an object" );
	}

	return result;
}

inline std::optional< element > field( const element json, const std::string_view key )
{
	const auto fields { object( json ) };
	element value {};
	const auto error { fields.at_key( key ).get( value ) };
	if ( error == simdjson::NO_SUCH_FIELD )
	{
		return std::nullopt;
	}

	if ( error )
	{
		invalid( key, simdjson::error_message( error ) );
	}

	return value;
}

inline element required( const element json, const std::string_view key )
{
	const auto value { field( json, key ) };
	if ( !value )
	{
		invalid( key, "required field is missing" );
	}

	return value.value();
}

template < typename T >
T decode( element json )
{
	if constexpr ( std::is_same_v< T, std::string > )
	{
		std::string_view value {};
		if ( json.get_string().get( value ) ) invalid( "string", "expected a string" );

		return std::string { value };
	}
	else if constexpr ( std::is_same_v< T, bool > )
	{
		bool value {};
		if ( json.get_bool().get( value ) ) invalid( "boolean", "expected a boolean" );

		return value;
	}
	else if constexpr ( std::is_integral_v< T > )
	{
		if constexpr ( std::is_unsigned_v< T > )
		{
			std::uint64_t value {};
			if ( json.get_uint64().get( value ) ) invalid( "integer", "expected a nonnegative integer" );

			if ( value > std::numeric_limits< T >::max() ) invalid( "integer", "out of range" );

			return static_cast< T >( value );
		}
		else
		{
			std::int64_t value {};
			if ( json.get_int64().get( value ) ) invalid( "integer", "expected an integer" );

			if ( value < std::numeric_limits< T >::min() || value > std::numeric_limits< T >::max() )
			{
				invalid( "integer", "out of range" );
			}

			return static_cast< T >( value );
		}
	}
	else if constexpr ( std::is_same_v< T, double > )
	{
		double value {};
		if ( json.get_double().get( value ) ) invalid( "number", "expected a number" );

		if ( !std::isfinite( value ) ) invalid( "number", "must be finite" );

		return value;
	}
	else
	{
		return T { json };
	}
}

template < typename T >
T read( const element json, const std::string_view key )
{
	try
	{
		return decode< T >( required( json, key ) );
	}
	catch ( const std::runtime_error& error )
	{
		invalid( key, error.what() );
	}
}

template < typename T >
std::optional< T > optional( const element json, const std::string_view key )
{
	const auto value { field( json, key ) };
	if ( !value ) return std::nullopt;

	return read< T >( json, key );
}

template < typename T >
std::vector< T > array( const element json )
{
	simdjson::dom::array values {};
	if ( json.get_array().get( values ) ) invalid( "array", "expected an array" );

	std::vector< T > result {};
	std::size_t index {};
	for ( const auto value : values )
	{
		try
		{
			result.emplace_back( decode< T >( value ) );
		}
		catch ( const std::runtime_error& error )
		{
			invalid( std::to_string( index ), error.what() );
		}

		++index;
	}

	return result;
}

template < typename T >
std::vector< T > list( const element json, const std::string_view key, const bool required = false )
{
	const auto value { field( json, key ) };
	if ( !value )
	{
		if ( required ) invalid( key, "required array is missing" );

		return {};
	}

	try
	{
		return array< T >( value.value() );
	}
	catch ( const std::runtime_error& error )
	{
		invalid( key, error.what() );
	}
}

} // namespace idhan::downloader::manifest_json
