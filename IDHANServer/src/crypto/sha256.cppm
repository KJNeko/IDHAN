//
// Created by kj16609 on 3/18/25.
//

module;

#include <QByteArray>
#include <QCryptographicHash>
#include <QVarLengthArray>

#include <openssl/sha.h>

#include <array>
#include <cassert>
#include <cstring>
#include <istream>
#include <vector>

#include "decodeHex.hpp"
#include "drogon/orm/Field.h"
#include "drogon/orm/SqlBinder.h"
#include "fgl/defines.hpp"

export module sha256;

export namespace idhan
{

class SHA256
{
	std::array< std::byte, ( 256 / 8 ) > m_data {};

	SHA256() = delete;

	explicit SHA256( const std::byte* data );
	SHA256( const std::string_view& data );

	friend SHA256 createFromIStream( std::istream& istream );

	explicit SHA256( std::array< std::byte, ( 256 / 8 ) >&& data ) :
	  m_data( std::forward< decltype( m_data ) >( data ) )
	{}

  public:

	std::array< std::byte, ( 256 / 8 ) > data() const { return m_data; };

	//! Supplied so we can work with drogon until I figure out how the fuck to overload their operators.
	inline std::vector< char > toVec() const
	{
		std::vector< char > data {};
		data.resize( m_data.size() );
		std::memcpy( data.data(), m_data.data(), m_data.size() );
		return data;
	}

	explicit SHA256( const drogon::orm::Field& field );

	static SHA256 hash( const std::string_view& str );

	std::string hex() const;

	//! Turns a HEX string into a SHA256 object. Str must be exactly (256 / 8) * 2, 64 characters long
	static SHA256 fromHex( const std::string& str );
	static SHA256 fromBuffer( const std::vector< std::byte >& data );

	explicit SHA256( QIODevice* io );
};

} // namespace idhan

namespace drogon::orm::internal
{

template <>
inline SqlBinder::self& SqlBinder::operator<< < idhan::SHA256 >( idhan::SHA256&& sha256 )
{
	const auto data { sha256.data() };
	std::vector< char > binary {};
	binary.resize( data.size() );
	std::memcpy( binary.data(), data.data(), data.size() );

	return *this << binary;
}

inline SqlBinder& operator<<( SqlBinder& binder, idhan::SHA256&& sha256 )
{
	const auto data { sha256.data() };
	std::vector< char > binary {};
	binary.resize( data.size() );
	std::memcpy( binary.data(), data.data(), data.size() );

	return binder << binary;
}

} // namespace drogon::orm::internal

namespace idhan
{
inline drogon::orm::internal::SqlBinder& operator<<( drogon::orm::internal::SqlBinder& binder, idhan::SHA256&& sha256 )
{
	const auto data { sha256.data() };
	std::vector< char > binary {};
	binary.resize( data.size() );
	std::memcpy( binary.data(), data.data(), data.size() );

	return binder << binary;
}
} // namespace idhan

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

SHA256 SHA256::fromBuffer( const std::vector< std::byte >& data )
{
	if ( data.size() != ( 256 / 8 ) ) throw std::runtime_error( "Invalid size" );
	return SHA256( data.data() );
}

SHA256::SHA256( QIODevice* io ) : m_data()
{
	QCryptographicHash hasher { QCryptographicHash::Sha256 };
	hasher.addData( io );

	const auto result { hasher.result() };
	std::memcpy( m_data.data(), result.data(), result.size() );
}

} // namespace idhan
