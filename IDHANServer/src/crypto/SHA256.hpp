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

namespace idhan
{
class FileIOUring;
}

namespace idhan
{

//! IDHAN's 32-byte content address for records and stored files.
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

	static constexpr std::size_t size() { return ( 256 / 8 ); }

	std::array< std::byte, ( 256 / 8 ) > data() const;

	//! Supplied for drogon interop, pending a proper operator overload.
	std::vector< char > toVec() const;

	bool operator==( const SHA256& other ) const;

	//! Lexicographic unsigned byte ordering; a valid strict weak ordering for use as a map/set key.
	bool operator<( const SHA256& other ) const;

	[[nodiscard]] std::string hex() const;

	[[nodiscard]] static std::expected< SHA256, drogon::HttpResponsePtr > fromHex( const std::string& str );
	[[nodiscard]] static SHA256 fromBuffer( const std::vector< std::byte >& data );
	[[nodiscard]] static SHA256 fromBuffer( const std::array< std::byte, 256 / 8 >& data );
	[[nodiscard]] static SHA256 fromPgCol( const drogon::orm::Field& field );
	[[nodiscard]] static drogon::Task< std::expected< SHA256, drogon::HttpResponsePtr > > fromDB(
		RecordID record_id,
		drogon::orm::DbClientPtr db );

	[[nodiscard]] static SHA256 hash( const std::byte* data, std::size_t size );
	[[nodiscard]] static drogon::Task< SHA256 > hashCoro( std::shared_ptr< FileIOUring > io_uring );

	[[nodiscard]] static SHA256 hash( const std::vector< std::byte >& data )
	{
		return hash( data.data(), data.size() );
	}
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
