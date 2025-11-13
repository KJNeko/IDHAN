//
// Created by kj16609 on 3/20/25.
//

#include "SHA256.hpp"

#include <expected>
#include <fstream>
#include <istream>

#include "../filesystem/io/IOUring.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "decodeHex.hpp"
#include "fgl/defines.hpp"

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

std::string SHA256::hex() const
{
	std::string str {};
	str.reserve( m_data.size() );
	for ( auto i : m_data ) str += format_ns::format( "{:02x}", static_cast< std::uint8_t >( i ) );
	return str;
}

std::expected< SHA256, drogon::HttpResponsePtr > SHA256::fromHex( const std::string& str )
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

SHA256 SHA256::fromBuffer( const std::vector< std::byte >& data )
{
	if ( data.size() != ( 256 / 8 ) )
		throw std::runtime_error(
			format_ns::format( "Invalid size. SHA256::fromBuffer expects a size of 32 bytes was {}", data.size() ) );
	return SHA256( data.data() );
}

SHA256 SHA256::fromPgCol( const drogon::orm::Field& field )
{
	return { field };
}

drogon::Task< std::expected< SHA256, drogon::HttpResponsePtr > > SHA256::fromDB( RecordID record_id, DbClientPtr db )
{
	const auto result { co_await db->execSqlCoro( "SELECT sha256 FROM records WHERE record_id = $1", record_id ) };

	if ( result.empty() ) co_return std::unexpected( createBadRequest( "Record not found" ) );

	co_return SHA256::fromPgCol( result[ 0 ][ 0 ] );
}

/*
SHA256::SHA256( QIODevice* io ) : m_data()
{
	QCryptographicHash hasher { QCryptographicHash::Sha256 };
	hasher.addData( io );

	const auto result { hasher.result() };
	std::memcpy( m_data.data(), result.data(), result.size() );
}
*/

SHA256 SHA256::hashFile( const std::filesystem::path& path )
{
	// TODO: Switch to mmap instead
	if ( std::ifstream ifs( path, std::ios_base::ate | std::ios_base::binary ); ifs )
	{
		std::vector< std::byte > data {};
		data.resize( ifs.tellg() );
		ifs.seekg( 0 );
		ifs.read( reinterpret_cast< char* >( data.data() ), data.size() );

		return SHA256::hash( data );
	}

	throw std::runtime_error( "Failed to open file" );
}

SHA256 SHA256::hash( const std::byte* data, const std::size_t size )
{
	QCryptographicHash hasher { QCryptographicHash::Sha256 };

	const QByteArrayView view { data, static_cast< qsizetype >( size ) };

	hasher.addData( view );

	const auto result { hasher.result() };

	std::vector< std::byte > out_data {};
	out_data.resize( 256 / 8 );

	FGL_ASSERT( out_data.size() == static_cast< std::size_t >( result.size() ), "Invalid size" );

	std::memcpy( out_data.data(), result.data(), static_cast< std::size_t >( result.size() ) );

	return SHA256::fromBuffer( out_data );
}

drogon::Task< SHA256 > SHA256::hashCoro( FileIOUring io_uring )
{
	QCryptographicHash hasher { QCryptographicHash::Sha256 };

	constexpr auto block_size { 1024 * 1024 };

	for ( std::size_t i = 0; i < io_uring.size(); i += block_size )
	{
		const auto data { co_await io_uring.read( i, block_size ) };

		const QByteArrayView view { data.data(), static_cast< qsizetype >( data.size() ) };

		hasher.addData( view );
	}

	const auto result { hasher.result() };

	std::vector< std::byte > out_data {};
	out_data.resize( 256 / 8 );

	FGL_ASSERT( out_data.size() == static_cast< std::size_t >( result.size() ), "Invalid size" );

	std::memcpy( out_data.data(), result.data(), static_cast< std::size_t >( result.size() ) );

	co_return SHA256::fromBuffer( out_data );
}

} // namespace idhan
