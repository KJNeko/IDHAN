//
// Created by kj16609 on 10/27/25.
//

#include <expected>

#include "errors/ErrorInfo.hpp"
#include "tags.hpp"

namespace idhan
{

drogon::Task< std::expected< NamespaceID, IDHANError > > createNamespace( std::string str, drogon::orm::DbClientPtr db )
{
	const auto search_result { co_await findNamespace( str, db ) };
	if ( search_result ) co_return *search_result;

	const auto insert_result {
		co_await db
			->execSqlCoro( "INSERT INTO tag_namespaces (namespace_text) VALUES ($1) RETURNING namespace_id", str )
	};

	if ( insert_result.empty() ) co_return std::unexpected( createError( "Failed to create namespace {}", str ) );

	co_return insert_result[ 0 ][ 0 ].as< NamespaceID >();
}

} // namespace idhan