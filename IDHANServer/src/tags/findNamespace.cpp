//
// Created by kj16609 on 10/27/25.
//
#include "tags.hpp"

namespace idhan
{

drogon::Task< std::optional< NamespaceID > > findNamespace( std::string str, drogon::orm::DbClientPtr db )
{
	const auto result {
		co_await db->execSqlCoro( "SELECT namespace_id FROM tag_namespaces WHERE namespace_text = $1", str )
	};

	if ( result.empty() ) co_return std::nullopt;

	co_return result[ 0 ][ 0 ].as< NamespaceID >();
}

} // namespace idhan
