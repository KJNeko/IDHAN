//
// Created by kj16609 on 3/20/25.
//

#include "SHA256.hpp"

#include <expected>
#include <fstream>
#include <istream>

#include "api/helpers/createBadRequest.hpp"
#include "fgl/defines.hpp"

namespace idhan
{

SHA256::SHA256( const std::byte* data ) : m_data()
{
	std::memcpy( m_data.data(), data, m_data.size() );
}

SHA256::SHA256( const std::string_view& data ) : m_data()
{}

SHA256::SHA256( const drogon::orm::Field& field )
{
	const auto data { field.as< std::vector< char > >() };

	FGL_ASSERT(
		data.size() == m_data.size(), std::format( "Invalid size. Expected {} got {}", m_data.size(), data.size() ) );

	std::memcpy( m_data.data(), data.data(), data.size() );
}

std::string SHA256::hex() const
{
	std::string str {};
	str.reserve( m_data.size() );
	for ( std::size_t i = 0; i < m_data.size(); ++i )
		str += std::format( "{:02x}", static_cast< std::uint8_t >( m_data[ i ] ) );
	return str;
}

std::expected< SHA256, drogon::HttpResponsePtr > SHA256::fromHex( const std::string& str )
{
	// 0xFF = 0b11111111
	FGL_ASSERT( str.size() == ( 256 / 8 * 2 ), "Hex string must be exactly 64 characters log" );
	if ( str.size() != ( 256 / 8 * 2 ) )
		return std::unexpected( createBadRequest( "Hex string must be exactly 64 characters long" ) );

	std::array< std::byte, ( 256 / 8 ) > bytes {};

	for ( std::size_t i = 0; i < str.size(); i += 2 )
	{
		bytes[ i / 2 ] = static_cast< std::byte >( decodeHexCharacters( str[ i ], str[ i + 1 ] ) );
	}

	return SHA256( std::move( bytes ) );
}

SHA256 SHA256::fromBuffer( const std::vector< std::byte >& data )
{
	if ( data.size() != ( 256 / 8 ) )
		throw std::runtime_error(
			std::format( "Invalid size. SHA256::fromBuffer expects a size of 32 bytes was {}", data.size() ) );
	return SHA256( data.data() );
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

SHA256 SHA256::hash( const std::filesystem::path& path )
{
	//TODO: Switch to mmap instead
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

	FGL_ASSERT( out_data.size() == result.size(), "Invalid size" );

	std::memcpy( out_data.data(), result.data(), result.size() );

	return SHA256::fromBuffer( out_data );
}

} // namespace idhan