#include "idhan/SHA256.hpp"

#include <format>
#include <optional>

#include "idhan/SHA256Hasher.hpp"

namespace idhan
{

std::optional< unsigned char > hexDigit( const char character )
{
	constexpr std::string_view characters { "0123456789abcdefABCDEF" };
	constexpr std::array< unsigned char, 22 > values {
		{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 10, 11, 12, 13, 14, 15 }
	};
	const auto index { characters.find( character ) };
	if ( index != std::string_view::npos ) return values[ index ];

	return std::nullopt;
}

std::expected< SHA256, std::string > SHA256::fromHex( const std::string_view str )
{
	if ( str.size() != size() * 2 )
		return std::unexpected( std::format( "Hex string must be exactly 64 characters long, was {}", str.size() ) );

	std::array< std::byte, size() > bytes {};
	for ( std::size_t index {}; index < bytes.size(); ++index )
	{
		const auto high { hexDigit( str[ index * 2 ] ) };
		if ( !high ) return std::unexpected( std::format( "Invalid hex character at position {}", index * 2 ) );

		const auto low { hexDigit( str[ index * 2 + 1 ] ) };
		if ( !low ) return std::unexpected( std::format( "Invalid hex character at position {}", index * 2 + 1 ) );

		bytes[ index ] = static_cast< std::byte >( ( *high << 4 ) | *low );
	}

	return SHA256::fromBuffer( bytes );
}

SHA256 SHA256::hash( const std::span< const std::byte > data )
{
	SHA256Hasher hasher;
	hasher.update( data );
	return hasher.finish();
}

} // namespace idhan
