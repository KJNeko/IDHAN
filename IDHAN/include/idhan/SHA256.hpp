#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <stdexcept>
#include <string>
#include <vector>

namespace idhan
{

class SHA256
{
	std::array< std::byte, 256 / 8 > m_data {};

	explicit SHA256( const std::array< std::byte, 256 / 8 >& data ) : m_data( data ) {}

  public:

	SHA256() = default;

	[[nodiscard]] static constexpr std::size_t size() { return 256 / 8; }

	[[nodiscard]] const std::array< std::byte, 256 / 8 >& data() const { return m_data; }

	[[nodiscard]] std::vector< char > toVec() const
	{
		std::vector< char > result;
		result.reserve( m_data.size() );
		for ( const auto byte : m_data )
			result.push_back( static_cast< char >( std::to_integer< unsigned char >( byte ) ) );

		return result;
	}

	[[nodiscard]] bool operator==( const SHA256& other ) const { return m_data == other.m_data; }

	[[nodiscard]] bool operator<( const SHA256& other ) const { return m_data < other.m_data; }

	[[nodiscard]] std::string hex() const
	{
		constexpr char characters[] { "0123456789abcdef" };
		std::string result( m_data.size() * 2, '0' );
		for ( std::size_t index {}; index < m_data.size(); ++index )
		{
			const auto byte { std::to_integer< unsigned char >( m_data[ index ] ) };
			result[ index * 2 ] = characters[ byte >> 4 ];
			result[ index * 2 + 1 ] = characters[ byte & 0x0fU ];
		}

		return result;
	}

	//! \return The parsed hash, or an error message if \p str is not exactly 64 hex characters.
	[[nodiscard]] static std::expected< SHA256, std::string > fromHex( const std::string& );

	[[nodiscard]] static SHA256 fromBuffer( const std::vector< std::byte >& data )
	{
		if ( data.size() != size() ) throw std::runtime_error( "SHA256 requires exactly 32 bytes" );

		std::array< std::byte, 256 / 8 > bytes {};
		std::copy( data.begin(), data.end(), bytes.begin() );
		return SHA256 { bytes };
	}

	[[nodiscard]] static SHA256 fromBuffer( const std::array< std::byte, 256 / 8 >& data ) { return SHA256 { data }; }

	[[nodiscard]] static SHA256 hash( const std::byte*, std::size_t );
};
} // namespace idhan

template <>
struct std::hash< idhan::SHA256 >
{
	std::size_t operator()( const idhan::SHA256& sha ) const noexcept
	{
		std::size_t seed {};
		for ( const auto byte : sha.data() )
			seed ^= std::to_integer< std::size_t >( byte ) + 0x9e3779b9 + ( seed << 6 ) + ( seed >> 2 );

		return seed;
	}
};
