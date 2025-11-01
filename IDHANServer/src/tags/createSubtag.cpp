//
// Created by kj16609 on 10/27/25.
//
#include "tags.hpp"

namespace idhan
{

drogon::Task< std::expected< SubtagID, IDHANError > > createSubtag( std::string str, drogon::orm::DbClientPtr db )
{
	const auto search_result { co_await findSubtag( str, db ) };
	if ( search_result ) co_return *search_result;

	const auto insert_result {
		co_await db->execSqlCoro( "INSERT INTO tag_subtags (subtag_text) VALUES ($1) RETURNING subtag_id", str )
	};

	if ( insert_result.empty() ) co_return std::unexpected( createError( "Failed to create subtag {}", str ) );

	co_return insert_result[ 0 ][ 0 ].as< NamespaceID >();
}

} // namespace idhan
