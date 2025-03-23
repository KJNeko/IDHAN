//
// Created by kj16609 on 3/18/25.
//

#pragma once

#include <QCryptographicHash>

#include <openssl/sha.h>

#include <array>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <vector>

#include "decodeHex.hpp"
#include "drogon/orm/Field.h"
#include "drogon/orm/SqlBinder.h"

namespace idhan
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

	SHA256( const drogon::orm::Field& field );

	std::string hex() const;

	//! Turns a HEX string into a SHA256 object. Str must be exactly (256 / 8) * 2, 64 characters long
	static SHA256 fromHex( const std::string& str );
	static SHA256 fromBuffer( const std::vector< std::byte >& data );

	inline static SHA256 hash( const std::vector< std::byte >& data ) { return hash( data.data(), data.size() ); }

	static SHA256 hash( const std::filesystem::path& path );
	static SHA256 hash( const std::byte* data, const std::size_t size );
};

} // namespace idhan
