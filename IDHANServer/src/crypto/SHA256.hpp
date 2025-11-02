//
// Created by kj16609 on 3/18/25.
//

#pragma once

#include <QCryptographicHash>

#include <drogon/HttpResponse.h>
#include <drogon/orm/DbClient.h>
#include <drogon/orm/Field.h>
#include <drogon/orm/SqlBinder.h>
#include <drogon/utils/coroutine.h>
#include <openssl/sha.h>

#include <array>
#include <cassert>
#include <cstring>
#include <expected>
#include <filesystem>
#include <vector>

#include "IDHANTypes.hpp"
#include "db/dbTypes.hpp"

namespace idhan
{
class FileIOUring;
}

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

	static constexpr std::size_t size() { return ( 256 / 8 ); }

	SHA256( const drogon::orm::Field& field );

	SHA256& operator=( const SHA256& other ) = default;
	SHA256( const SHA256& other ) = default;

	SHA256& operator=( SHA256&& other ) = default;
	SHA256( SHA256&& other ) = default;

	std::array< std::byte, ( 256 / 8 ) > data() const { return m_data; }

	//! Supplied so we can work with drogon until I figure out how the fuck to overload their operators.
	inline std::vector< char > toVec() const
	{
		std::vector< char > data {};
		data.resize( m_data.size() );
		std::memcpy( data.data(), m_data.data(), m_data.size() );
		return data;
	}

	bool operator==( const SHA256& other ) const
	{
		return std::memcmp( m_data.data(), other.m_data.data(), m_data.size() ) == 0;
	}

	bool operator<( const SHA256& other ) const
	{
		for ( std::size_t i = 0; i < m_data.size(); ++i )
		{
			const auto& a = m_data[ i ];
			const auto& b = other.m_data[ i ];
			if ( a < b ) return true;
		}
		return false;
	}

	std::string hex() const;

	//! Turns a HEX string into a SHA256 object. Str must be exactly (256 / 8) * 2, 64 characters long
	static std::expected< SHA256, drogon::HttpResponsePtr > fromHex( const std::string& str );
	//! Takes the byte representation of a hash from a buffer.
	static SHA256 fromBuffer( const std::vector< std::byte >& data );
	static SHA256 fromPgCol( const drogon::orm::Field& field );
	static drogon::Task< std::expected< SHA256, drogon::HttpResponsePtr > > fromDB(
		RecordID record_id,
		DbClientPtr db );

	static SHA256 hash( const std::byte* data, std::size_t size );
	static drogon::Task< SHA256 > hashCoro( FileIOUring uring );

	static SHA256 hash( const std::vector< std::byte >& data ) { return hash( data.data(), data.size() ); }

	static SHA256 hashFile( const std::filesystem::path& path );
};

} // namespace idhan

template <>
struct std::hash< idhan::SHA256 >
{
	std::size_t operator()( const idhan::SHA256& sha ) const noexcept
	{
		std::size_t seed = 0;
		const auto& data = sha.data();
		for ( const auto& byte : data )
		{
			seed ^= std::hash< std::byte >()( byte ) + 0x9e3779b9 + ( seed << 6 ) + ( seed >> 2 );
		}
		return seed;
	}
};
