#pragma once
#include <charconv>
#include <concepts>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ValidatedJson.hpp"

namespace idhan::http
{
class RequestParameters
{
	std::vector< std::pair< std::string, std::string > > m_values;

  public:

	RequestParameters() : m_values {} {}

	[[nodiscard]] static std::expected< RequestParameters, ParseError > parseQuery( std::string_view );
	void append( std::string name, std::string value );
	template < typename T >
	[[nodiscard]] std::expected< T, ParseError > scalar( std::string_view ) const;
	template < typename T >
	[[nodiscard]] std::expected< std::optional< T >, ParseError > optional( std::string_view ) const;
	template < typename T >
	[[nodiscard]] std::expected< std::vector< T >, ParseError > vector( std::string_view ) const;
	[[nodiscard]] std::span< const std::pair< std::string, std::string > > values() const noexcept;
};

template < typename T >
std::expected< T, ParseError > parseParameter( const std::string_view name, const std::string_view text )
{
	if constexpr ( std::same_as< T, std::string > )
		return std::string { text };

	else if constexpr ( std::same_as< T, bool > )
	{
		if ( text == "true" || text == "1" ) return true;
		if ( text == "false" || text == "0" ) return false;

		return std::unexpected( ParseError { std::string { name }, "must be true, false, 1, or 0" } );
	}

	else if constexpr ( std::integral< T > )
	{
		T value {};
		const auto parsed { std::from_chars( text.data(), text.data() + text.size(), value ) };
		if ( parsed.ec == std::errc {} && parsed.ptr == text.data() + text.size() ) return value;
	}

	return std::unexpected( ParseError { std::string { name }, "must be a valid value" } );
}

template < typename T >
std::expected< T, ParseError > RequestParameters::scalar( const std::string_view name ) const
{
	const std::pair< std::string, std::string >* result {};
	for ( const auto& value : m_values )
	{
		if ( value.first != name ) continue;

		if ( result ) return std::unexpected( ParseError { std::string { name }, "must not be repeated" } );

		result = &value;
	}

	if ( !result ) return std::unexpected( ParseError { std::string { name }, "is required" } );

	return parseParameter< T >( name, result->second );
}

template < typename T >
std::expected< std::optional< T >, ParseError > RequestParameters::optional( const std::string_view name ) const
{
	const std::pair< std::string, std::string >* result {};
	for ( const auto& value : m_values )
	{
		if ( value.first != name ) continue;

		if ( result ) return std::unexpected( ParseError { std::string { name }, "must not be repeated" } );

		result = &value;
	}

	if ( !result ) return std::optional< T > {};

	auto parsed { parseParameter< T >( name, result->second ) };
	if ( !parsed ) return std::unexpected( std::move( parsed.error() ) );

	return std::optional< T > { std::move( *parsed ) };
}

template < typename T >
std::expected< std::vector< T >, ParseError > RequestParameters::vector( const std::string_view name ) const
{
	std::vector< T > result;
	for ( const auto& value : m_values )
	{
		if ( value.first != name ) continue;

		auto parsed { parseParameter< T >( name, value.second ) };
		if ( !parsed ) return std::unexpected( std::move( parsed.error() ) );

		result.push_back( std::move( *parsed ) );
	}

	return result;
}
} // namespace idhan::http
