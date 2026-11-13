#pragma once

#include <cstdint>
#include <type_traits>

#include "idhan/http/request/RequestParameters.hpp"

namespace idhan::http::validation
{

template < std::integral T = std::int64_t >
struct Integer
{
	static_assert( !std::same_as< T, bool > );
	using Value = T;
	static constexpr bool positive { false };

	[[nodiscard]] static std::expected< void, ParseError > validate(
		const std::string_view name,
		const std::string_view text )
	{
		auto value_e { parseParameter< T >( name, text ) };
		if ( !value_e.has_value() ) return std::unexpected( std::move( value_e.error() ) );

		return {};
	}
};

template < std::integral T = std::int64_t >
struct PositiveInteger : Integer< T >
{
	static constexpr bool positive { true };

	[[nodiscard]] static std::expected< void, ParseError > validate(
		const std::string_view name,
		const std::string_view text )
	{
		auto value_e { parseParameter< T >( name, text ) };
		if ( !value_e.has_value() ) return std::unexpected( std::move( value_e.error() ) );

		if ( value_e.value() <= 0 )
			return std::unexpected( ParseError { std::string { name }, "must be a positive integer" } );

		return {};
	}
};

struct UUID
{
	using Value = std::string;
	static constexpr std::string_view format { "uuid" };

	[[nodiscard]] static std::expected< void, ParseError > validate(
		const std::string_view name,
		const std::string_view text )
	{
		if ( text.size() != 36 ) return std::unexpected( ParseError { std::string { name }, "must be a uuid" } );

		for ( std::size_t index {}; index < text.size(); ++index )
		{
			const auto character { text[ index ] };
			if ( index == 8 || index == 13 || index == 18 || index == 23 )
			{
				if ( character != '-' ) return std::unexpected( ParseError { std::string { name }, "must be a uuid" } );

				continue;
			}

			const bool hexadecimal {
				( character >= '0' && character <= '9' ) || ( character >= 'a' && character <= 'f' )
				|| ( character >= 'A' && character <= 'F' )
			};
			if ( !hexadecimal ) return std::unexpected( ParseError { std::string { name }, "must be a uuid" } );
		}
		return {};
	}
};

} // namespace idhan::http::validation
