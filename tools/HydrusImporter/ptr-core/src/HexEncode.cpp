#include "ptr/flatten/HexEncode.hpp"

namespace idhan::hydrus::ptr
{

std::string toHex( const std::span< const std::byte > bytes )
{
	constexpr char DIGITS[] = "0123456789abcdef";

	std::string out;
	out.reserve( bytes.size() * 2 );

	for ( const auto byte : bytes )
	{
		const auto value = std::to_integer< unsigned >( byte );
		out.push_back( DIGITS[ ( value >> 4 ) & 0xF ] );
		out.push_back( DIGITS[ value & 0xF ] );
	}

	return out;
}

} // namespace idhan::hydrus::ptr
