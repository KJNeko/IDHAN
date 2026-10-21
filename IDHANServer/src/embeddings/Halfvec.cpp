#include <cstdlib>

#include "resolveTerms.hpp"

namespace idhan::embeddings
{
std::vector< float > parseHalfvecLiteral( const std::string& literal )
{
	std::vector< float > values {};
	std::size_t cursor { literal.find( '[' ) };
	if ( cursor == std::string::npos ) return values;
	++cursor;
	while ( cursor < literal.size() && literal[ cursor ] != ']' )
	{
		const auto end { literal.find_first_of( ",]", cursor ) };
		if ( end == std::string::npos ) break;
		values.push_back( std::strtof( literal.substr( cursor, end - cursor ).c_str(), nullptr ) );
		cursor = literal[ end ] == ',' ? end + 1 : end;
	}
	return values;
}
} // namespace idhan::embeddings
