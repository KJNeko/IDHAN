#include "IDHANTypes.hpp"
#include "mime.hpp"

namespace idhan::mime
{

std::optional< MimeID > searchMimeType( const std::string& name, DbClientPtr db )
{
	const auto search_result { db->execSqlSync( "SELECT mime_id FROM mime WHERE name = $1", name ) };

	if ( search_result.size() > 0 )
	{
		return search_result[ 0 ][ 0 ].as< MimeID >();
	}

	return std::nullopt;
}

} // namespace idhan::mime
