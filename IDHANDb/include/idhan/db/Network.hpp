#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace idhan::db
{
// Explicit source order lets tests exercise both endian paths on any host.
template < std::endian Source, typename T >
constexpr T networkOrder( T value )
{
	static_assert( std::is_unsigned_v< T > );
	static_assert( Source == std::endian::little || Source == std::endian::big );
	if constexpr ( Source == std::endian::little ) return std::byteswap( value );
	return value;
}

template < typename T >
std::vector< std::byte > encodeNetwork( T value )
{
	static_assert( std::is_arithmetic_v< T > && !std::is_same_v< T, bool > );
	static_assert( sizeof( T ) == 2 || sizeof( T ) == 4 || sizeof( T ) == 8 );
	using Bits = std::conditional_t<
		sizeof( T ) == 2,
		std::uint16_t,
		std::conditional_t< sizeof( T ) == 4, std::uint32_t, std::uint64_t > >;
	const auto bits { networkOrder< std::endian::native >( std::bit_cast< Bits >( value ) ) };
	std::vector< std::byte > result( sizeof( T ) );
	std::memcpy( result.data(), &bits, sizeof( bits ) );
	return result;
}

template < typename T >
T decodeNetwork( std::span< const std::byte > bytes )
{
	static_assert( std::is_arithmetic_v< T > && !std::is_same_v< T, bool > );
	static_assert( sizeof( T ) == 2 || sizeof( T ) == 4 || sizeof( T ) == 8 );
	if ( bytes.size() != sizeof( T ) ) throw std::invalid_argument( "Invalid network scalar length" );

	using Bits = std::conditional_t<
		sizeof( T ) == 2,
		std::uint16_t,
		std::conditional_t< sizeof( T ) == 4, std::uint32_t, std::uint64_t > >;
	Bits bits;
	std::memcpy( &bits, bytes.data(), sizeof( bits ) );
	return std::bit_cast< T >( networkOrder< std::endian::native >( bits ) );
}
} // namespace idhan::db
