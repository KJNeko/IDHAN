//
// Created by kj16609 on 7/24/24.
//

#pragma once

#include <QByteArray>
#include <QVarLengthArray>

#include <openssl/sha.h>

#include <array>
#include <istream>

#include "drogon/orm/SqlBinder.h"

namespace drogon::orm
{
class Field;
}
class QIODevice;

namespace idhan
{

class SHA256
{
	std::array< std::byte, ( 256 / 8 ) > m_data {};

	SHA256() = delete;

	explicit SHA256( const std::byte* data );

	friend SHA256 createFromIStream( std::istream& istream );

	explicit SHA256( std::array< std::byte, ( 256 / 8 ) >&& data ) :
	  m_data( std::forward< decltype( m_data ) >( data ) )
	{}

  public:

	std::array< std::byte, ( 256 / 8 ) > data() const { return m_data; };

	//! Supplied so we can work with drogon until I figure out how the fuck to overload their operators.
	inline std::vector< char > toVec()
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
