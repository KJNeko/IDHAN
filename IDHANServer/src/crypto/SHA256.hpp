//
// Created by kj16609 on 3/18/25.
//

#pragma once

#include <QCryptographicHash>

#include <drogon/HttpResponse.h>
#include <openssl/sha.h>

#include <array>
#include <cassert>
#include <cstring>
#include <expected>
#include <filesystem>
#include <vector>

#include "../../../dependencies/drogon/orm_lib/tests/sqlite3/Blog.h"
#include "IDHANTypes.hpp"
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

	std::array< std::byte, ( 256 / 8 ) > data() const { return m_data; }

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
	static std::expected< SHA256, drogon::HttpResponsePtr > fromHex( const std::string& str );
	//! Takes the byte representation of a hash from a buffer.
	static SHA256 fromBuffer( const std::vector< std::byte >& data );
	static SHA256 fromPgCol( const drogon::orm::Field& field );
	static drogon::Task< std::expected< SHA256, drogon::HttpResponsePtr > >
		fromDB( RecordID record_id, drogon::orm::DbClientPtr db );

	static SHA256 hash( const std::byte* data, std::size_t size );

	static SHA256 hash( const std::vector< std::byte >& data ) { return hash( data.data(), data.size() ); }

	static SHA256 hashFile( const std::filesystem::path& path );
};

} // namespace idhan
