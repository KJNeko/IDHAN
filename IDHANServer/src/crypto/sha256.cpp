//
// Created by kj16609 on 7/30/24.
//

#include "sha256.hpp"

#include <QCryptographicHash>

#include <openssl/evp.h>

#include <cassert>
#include <cstring>
#include <vector>

#include "decodeHex.hpp"
#include "drogon/orm/Field.h"
#include "fgl/defines.hpp"

namespace idhan
{

SHA256::SHA256( const std::byte* data ) : m_data()
{
	std::memcpy( m_data.data(), data, m_data.size() );
}

SHA256::SHA256( const std::string_view& data ) : m_data()
{
	QCryptographicHash hasher { QCryptographicHash::Sha256 };

	const QByteArrayView view { data.data(), data.size() };

	hasher.addData( view );

	const auto result { hasher.result() };
	std::memcpy( m_data.data(), result.data(), result.size() );
}

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

SHA256 SHA256::fromHex( const std::string& str )
{
	// 0xFF = 0b11111111
	FGL_ASSERT( str.size() == ( 256 / 8 * 2 ), "Hex string must be exactly 64 characters log" );

	std::array< std::byte, ( 256 / 8 ) > bytes {};

	for ( std::size_t i = 0; i < str.size(); i += 2 )
	{
		bytes[ i / 2 ] = static_cast< std::byte >( decodeHexCharacters( str[ i ], str[ i + 1 ] ) );
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