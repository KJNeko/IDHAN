#include "idhan/db/formatSqlForLog.hpp"

#include <cctype>

namespace idhan::db
{

std::string formatSqlForLog( const std::string_view sql )
{
	std::string output;
	output.reserve( sql.size() );
	bool pending_space { false };
	for ( const char character : sql )
	{
		if ( std::isspace( static_cast< unsigned char >( character ) ) )
		{
			pending_space = !output.empty();
			continue;
		}

		if ( pending_space ) output.push_back( ' ' );
		output.push_back( character );
		pending_space = false;
	}
	return output;
}

} // namespace idhan::db
