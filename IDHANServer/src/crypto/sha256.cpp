//
// Created by kj16609 on 7/30/24.
//

#include "sha256.hpp"

#include <QCryptographicHash>

#include <openssl/evp.h>

#include <cassert>
#include <cstring>
#include <vector>

#include "drogon/orm/Field.h"
#include "fgl/defines.hpp"

namespace idhan
{

SHA256::SHA256( const std::byte* data ) : m_data()
{
	std::memcpy( m_data.data(), data, m_data.size() );
}

SHA256::SHA256( const std::string_view& data )
{}

SHA256::SHA256( const drogon::orm::Field& field )
{
	const auto data { field.as< std::vector< char > >() };

	assert( data.size() == m_data.size() );

	std::memcpy( m_data.data(), data.data(), data.size() );
}

SHA256 SHA256::hash( const std::string_view& str )
{
	SHA256 sha256 { str };

	return sha256;
}

std::string SHA256::hex() const
{
	std::string str {};
	str.reserve( m_data.size() );
	for ( std::size_t i = 0; i < m_data.size(); ++i )
		str += std::format( "{:02x}", static_cast< std::uint8_t >( m_data[ i ] ) );
	return str;
}

//! Decodes a single hex character
constexpr std::uint8_t decodeHexChar( const char hex )
{
	switch ( hex )
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
		case 'A':
			[[fallthrough]];
		case 'a':
			return 10;
		case 'B':
			[[fallthrough]];
		case 'b':
			return 11;
		case 'C':
			[[fallthrough]];
		case 'c':
			return 12;
		case 'D':
			[[fallthrough]];
		case 'd':
			return 13;
		case 'E':
			[[fallthrough]];
		case 'e':
			return 14;
		case 'F':
			[[fallthrough]];
		case 'f':
			return 15;
		default:
			throw std::invalid_argument( "invalid hex char" );
	}
}

constexpr std::uint8_t decodeHexPair( const char left, const char right )
{
	return decodeHexChar( right ) | ( decodeHexChar( left ) << 4 );
}

static_assert( decodeHexChar( 'F' ) == 0xF );
static_assert( decodeHexChar( '0' ) == 0x0 );
static_assert( decodeHexChar( '5' ) == 0x5 );
static_assert( decodeHexChar( 'A' ) == 0xA );

static_assert( ( decodeHexChar( 'F' ) << 4 ) == 0xF0 );
static_assert( ( decodeHexChar( '0' ) << 4 ) == 0x00 );
static_assert( ( decodeHexChar( '5' ) << 4 ) == 0x50 );
static_assert( ( decodeHexChar( 'A' ) << 4 ) == 0xA0 );

static_assert( decodeHexPair( '0', '0' ) == 0x00 );
static_assert( decodeHexPair( 'F', '0' ) == 0xF0 );
static_assert( decodeHexPair( '5', 'A' ) == 0x5A );
static_assert( decodeHexPair( 'F', 'F' ) == 0xFF );

SHA256 SHA256::fromHex( const std::string& str )
{
	// 0xFF = 0b11111111
	FGL_ASSERT( str.size() == ( 256 / 8 * 2 ), "Hex string must be exactly 64 characters log" );

	std::array< std::byte, ( 256 / 8 ) > bytes {};

	for ( std::size_t i = 0; i < str.size(); i += 2 )
	{
		bytes[ i / 2 ] = static_cast< std::byte >( decodeHexPair( str[ i ], str[ i + 1 ] ) );
	}

	return SHA256( std::move( bytes ) );
}

SHA256::SHA256( QIODevice* io ) : m_data()
{
	QCryptographicHash hasher { QCryptographicHash::Sha256 };
	hasher.addData( io );

	const auto result { hasher.result() };
	std::memcpy( m_data.data(), result.data(), result.size() );
}

} // namespace idhan