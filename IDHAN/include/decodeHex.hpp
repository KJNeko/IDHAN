//
// Created by kj16609 on 2/20/25.
//
#pragma once

#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace idhan
{

constexpr std::uint8_t decodeHexCharacter( const char h )
{
	switch ( h )
	{
		case '0':
			return 0;
		case '1':
			return 1;
		case '2':
			return 2;
		case '3':
			return 3;
		case '4':
			return 4;
		case '5':
			return 5;
		case '6':
			return 6;
		case '7':
			return 7;
		case '8':
			return 8;
		case '9':
			return 9;
		case 'a':
			[[fallthrough]];
		case 'A':
			return 10;
		case 'b':
			[[fallthrough]];
		case 'B':
			return 11;
		case 'c':
			[[fallthrough]];
		case 'C':
			return 12;
		case 'd':
			[[fallthrough]];
		case 'D':
			return 13;
		case 'e':
			[[fallthrough]];
		case 'E':
			return 14;
		case 'f':
			[[fallthrough]];
		case 'F':
			return 15;
		default:
			throw std::invalid_argument( "Invalid character '" + std::to_string( h ) + "'" );
	}

	std::unreachable();
}

constexpr std::uint8_t decodeHexCharacters( const char left, const char right )
{
	const std::uint8_t left_char { static_cast< std::uint8_t >( decodeHexCharacter( left ) << 4 ) };
	const std::uint8_t right_char { decodeHexCharacter( right ) };

	const std::uint8_t result { static_cast< std::uint8_t >( left_char | right_char ) };

	return result;
}

static_assert( 0xFF == decodeHexCharacters( 'F', 'F' ) );
static_assert( 0xF1 == decodeHexCharacters( 'F', '1' ) );
static_assert( 0x21 == decodeHexCharacters( '2', '1' ) );
static_assert( 0x00 == decodeHexCharacters( '0', '0' ) );
static_assert( 0x61 == decodeHexCharacters( '6', '1' ) );

inline std::vector< std::byte > decodeHex( const std::string& str )
{
	std::vector< std::byte > result {};
	result.resize( str.size() / 2 );

	for ( std::size_t i = 0; i < str.size(); i += 2 )
	{
		const char first { str[ i ] };
		const char second { str[ i + 1 ] };

		result[ i / 2 ] = static_cast< std::byte >( decodeHexCharacters( first, second ) );
	}

	return result;
}

static_assert( decodeHexCharacter( 'F' ) == 0xF );
static_assert( decodeHexCharacter( '0' ) == 0x0 );
static_assert( decodeHexCharacter( '5' ) == 0x5 );
static_assert( decodeHexCharacter( 'A' ) == 0xA );

static_assert( ( decodeHexCharacter( 'F' ) << 4 ) == 0xF0 );
static_assert( ( decodeHexCharacter( '0' ) << 4 ) == 0x00 );
static_assert( ( decodeHexCharacter( '5' ) << 4 ) == 0x50 );
static_assert( ( decodeHexCharacter( 'A' ) << 4 ) == 0xA0 );

static_assert( decodeHexCharacters( '0', '0' ) == 0x00 );
static_assert( decodeHexCharacters( 'F', '0' ) == 0xF0 );
static_assert( decodeHexCharacters( '5', 'A' ) == 0x5A );
static_assert( decodeHexCharacters( 'F', 'F' ) == 0xFF );

} // namespace idhan