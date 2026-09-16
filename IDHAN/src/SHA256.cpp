#include "idhan/SHA256.hpp"

#include <format>

#include "crypto/simpleHasher.hpp"

namespace idhan
{

std::expected< SHA256, std::string > SHA256::fromHex( const std::string& str )
{
	if ( str.size() != ( 256 / 8 * 2 ) )
		return std::unexpected( std::format( "Hex string must be exactly 64 characters long, was {}", str.size() ) );

	try
	{
		return SHA256::fromBuffer( crypto::fromHex( str ) );
	}
	catch ( const std::exception& e )
	{
		return std::unexpected( e.what() );
	}
}

SHA256 SHA256::hash( const std::byte* data, const std::size_t size )
{
	return SHA256::fromBuffer( crypto::hashData( data, size ) );
}

} // namespace idhan
