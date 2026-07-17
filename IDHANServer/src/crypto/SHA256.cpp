//
// Created by kj16609 on 3/20/25.
//

#include "SHA256.hpp"

#include <openssl/evp.h>
#include <openssl/sha.h>

#include <expected>
#include <fstream>

#include "api/helpers/createBadRequest.hpp"
#include "crypto/simpleHasher.hpp"
#include "decodeHex.hpp"
#include "fgl/defines.hpp"
#include "filesystem/io/IOUring.hpp"

namespace idhan
{

SHA256::SHA256( const std::byte* data ) : m_data()
{
	std::memcpy( m_data.data(), data, m_data.size() );
}

SHA256::SHA256( const std::string_view& data ) : m_data()
{
	FGL_ASSERT( data.size() == m_data.size(), "Input data size was not correct" );
	std::memcpy( m_data.data(), data.data(), m_data.size() );
}

SHA256::SHA256( const drogon::orm::Field& field )
{
	if ( field.isNull() )
	{
		throw std::invalid_argument( "Field is null" );
	}

	const auto data { field.as< std::vector< char > >() };

	FGL_ASSERT(
		data.size() == m_data.size(),
		format_ns::format( "Invalid size. Expected {} got {}", m_data.size(), data.size() ) );

	std::memcpy( m_data.data(), data.data(), data.size() );
}

std::array< std::byte, ( 256 / 8 ) > SHA256::data() const
{
	return m_data;
}

std::vector< char > SHA256::toVec() const
{
	std::vector< char > data {};
	data.resize( m_data.size() );
	std::memcpy( data.data(), m_data.data(), m_data.size() );
	return data;
}

bool SHA256::operator==( const SHA256& other ) const
{
	for ( std::size_t i = 0; i < other.m_data.size(); ++i )
	{
		if ( m_data[ i ] != other.m_data[ i ] )
		{
			return false;
		}
	}
	return true;
}

bool SHA256::operator<( const SHA256& other ) const
{
	for ( std::size_t i = 0; i < other.m_data.size(); ++i )
	{
		if ( other.m_data[ i ] < this->m_data[ i ] ) return true;
		if ( other.m_data[ i ] > this->m_data[ i ] ) return false;
		// equal compare next byte
	}
	// all bytes are equal
	return false;
}

std::string SHA256::hex() const
{
	std::string str {};
	str.reserve( m_data.size() );
	for ( auto i : m_data ) str += format_ns::format( "{:02x}", static_cast< std::uint8_t >( i ) );
	return str;
}

std::expected< SHA256, drogon::HttpResponsePtr > SHA256::fromHex( const std::string& str )
{
	try
	{
		// 0xFF = 0b11111111
		// FGL_ASSERT( str.size() == ( 256 / 8 * 2 ), "Hex string must be exactly 64 characters log" );
		if ( str.size() != ( 256 / 8 * 2 ) )
			return std::unexpected(
				createBadRequest( "Hex string must be exactly 64 characters long, Was {}", str.size() ) );

		std::array< std::byte, ( 256 / 8 ) > bytes {};

		for ( std::size_t i = 0; i < str.size(); i += 2 )
		{
			bytes[ i / 2 ] = static_cast< std::byte >( decodeHexCharacters( str[ i ], str[ i + 1 ] ) );
		}

		SHA256 sha256 { std::move( bytes ) };

		return sha256;
	}
	catch ( std::exception& e )
	{
		return std::unexpected( createBadRequest( e.what() ) );
	}
}

SHA256 SHA256::fromBuffer( const std::vector< std::byte >& data )
{
	if ( data.size() != ( 256 / 8 ) )
		throw std::runtime_error(
			format_ns::format( "Invalid size. SHA256::fromBuffer expects a size of 32 bytes was {}", data.size() ) );
	return SHA256( data.data() );
}

SHA256 SHA256::fromBuffer( const std::array< std::byte, 256 / 8 >& data )
{
	return SHA256 { data };
}

SHA256 SHA256::fromPgCol( const drogon::orm::Field& field )
{
	return { field };
}

drogon::Task< std::expected< SHA256, drogon::HttpResponsePtr > > SHA256::fromDB( RecordID record_id, DbClientPtr db )
{
	const auto result { co_await db->execSqlCoro( "SELECT sha256 FROM records WHERE record_id = $1", record_id ) };

	if ( result.empty() ) co_return std::unexpected( createNotFound( "Record not found" ) );

	co_return SHA256::fromPgCol( result[ 0 ][ 0 ] );
}

SHA256 SHA256::hash( const std::byte* data, const std::size_t size )
{
	return SHA256::fromBuffer( crypto::hashData( data, size ) );
}

drogon::Task< SHA256 > SHA256::hashCoro( FileIOUring io_uring )
{
	constexpr auto block_size { 1024 * 1024 };

	const std::unique_ptr< EVP_MD_CTX, void ( * )( EVP_MD_CTX* ) > ctx {
		EVP_MD_CTX_new(), []( EVP_MD_CTX* ptr ) { EVP_MD_CTX_free( ptr ); }
	};
	if ( !ctx ) throw std::runtime_error( "EVP_MD_CTX_new() failed" );

	if ( EVP_DigestInit_ex( ctx.get(), EVP_sha256(), nullptr ) != 1 )
		throw std::runtime_error( "EVP_DigestInit_ex() failed" );

	for ( std::size_t i = 0; i < io_uring.size(); i += block_size )
	{
		const auto data { co_await io_uring.read( i, block_size ) };

		if ( EVP_DigestUpdate( ctx.get(), data.data(), data.size() ) != 1 )
			throw std::runtime_error( "EVP_DigestUpdate() failed" );
	}

	unsigned char hash[ EVP_MAX_MD_SIZE ];
	unsigned int hash_length { 0 };

	if ( EVP_DigestFinal_ex( ctx.get(), hash, &hash_length ) != 1 )
		throw std::runtime_error( "EVP_DigestFinal_ex() failed" );

	std::array< std::byte, 256 / 8 > out_data {};

	FGL_ASSERT( out_data.size() == static_cast< std::size_t >( hash_length ), "Invalid size" );

	std::memcpy( out_data.data(), hash, static_cast< std::size_t >( hash_length ) );

	co_return SHA256::fromBuffer( out_data );
}

} // namespace idhan
