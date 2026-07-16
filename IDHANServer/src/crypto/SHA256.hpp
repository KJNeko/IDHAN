//
// Created by kj16609 on 3/18/25.
//

#pragma once

#include <drogon/HttpResponse.h>
#include <drogon/orm/DbClient.h>
#include <drogon/orm/SqlBinder.h>
#include <drogon/utils/coroutine.h>

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

//! A 32-byte SHA-256 hash — IDHAN's content address for every record and stored file. Converts
//! to/from hex and PostgreSQL bytea columns, and is hashable for use as a map/set key.
class SHA256
{
	std::array< std::byte, ( 256 / 8 ) > m_data {};

	explicit SHA256( const std::byte* data );
	SHA256( const std::string_view& data );

	friend SHA256 createFromIStream( std::istream& istream );

	explicit SHA256( std::array< std::byte, ( 256 / 8 ) >&& data ) :
	  m_data( std::forward< decltype( m_data ) >( data ) )
	{}

	explicit SHA256( const std::array< std::byte, ( 256 / 8 ) >& data ) : m_data( data ) {}

  public:

	SHA256() = default;

	SHA256( const drogon::orm::Field& field );

	SHA256& operator=( const SHA256& other ) = default;
	SHA256( const SHA256& other ) = default;

	SHA256& operator=( SHA256&& other ) = default;
	SHA256( SHA256&& other ) = default;

	//! \return The hash length in bytes (32).
	static constexpr std::size_t size() { return ( 256 / 8 ); }

	//! \return A copy of the raw 32 hash bytes.
	std::array< std::byte, ( 256 / 8 ) > data() const;

	//! Supplied so we can work with drogon until I figure out how the fuck to overload their operators.
	std::vector< char > toVec() const;

	bool operator==( const SHA256& other ) const;

	//! Lexicographic unsigned byte ordering; a valid strict weak ordering for use as a map/set key.
	bool operator<( const SHA256& other ) const;

	//! \return The lowercase 64-character hex representation of the hash.
	[[nodiscard]] std::string hex() const;

	//! Turns a HEX string into a SHA256 object. Str must be exactly (256 / 8) * 2, 64 characters long
	[[nodiscard]] static std::expected< SHA256, drogon::HttpResponsePtr > fromHex( const std::string& str );
	//! Takes the byte representation of a hash from a buffer.
	[[nodiscard]] static SHA256 fromBuffer( const std::vector< std::byte >& data );
	//! \copydoc fromBuffer(const std::vector<std::byte>&)
	[[nodiscard]] static SHA256 fromBuffer( const std::array< std::byte, 256 / 8 >& data );
	//! Builds a SHA256 from a PostgreSQL bytea field.
	[[nodiscard]] static SHA256 fromPgCol( const drogon::orm::Field& field );
	//! Fetches the SHA-256 of \p record_id from the database. \return the hash, or an error response.
	[[nodiscard]] static drogon::Task< std::expected< SHA256, drogon::HttpResponsePtr > > fromDB(
		RecordID record_id,
		DbClientPtr db );

	//! Computes the SHA-256 of \p size bytes at \p data.
	[[nodiscard]] static SHA256 hash( const std::byte* data, std::size_t size );
	//! Computes the SHA-256 of a file streamed via io_uring.
	[[nodiscard]] static drogon::Task< SHA256 > hashCoro( FileIOUring uring );

	//! \copydoc hash(const std::byte*,std::size_t)
	[[nodiscard]] static SHA256 hash( const std::vector< std::byte >& data )
	{
		return hash( data.data(), data.size() );
	}
};

} // namespace idhan

//! std::hash specialization so SHA256 can be used as a key in unordered containers.
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
