//
// Created by kj16609 on 10/27/25.
//
#include "tags.hpp"

namespace idhan
{

drogon::Task< std::optional< SubtagID > > findSubtag( std::string str, drogon::orm::DbClientPtr db )
{
	const auto result { co_await db->execSqlCoro( "SELECT subtag_id FROM tag_subtags WHERE subtag_text = $1", str ) };

	if ( result.empty() ) co_return std::nullopt;

	co_return result[ 0 ][ 0 ].as< SubtagID >();
}

} // namespace idhan
