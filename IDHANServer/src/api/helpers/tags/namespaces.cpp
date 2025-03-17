//
// Created by kj16609 on 3/13/25.
//

#include "namespaces.hpp"

#include "api/helpers/createBadRequest.hpp"

namespace idhan
{

drogon::Task< std::optional< NamespaceID > > searchNamespace( const std::string& str, drogon::orm::DbClientPtr db )
{
	const auto result {
		co_await db->execSqlCoro( "SELECT namespace_id FROM tag_namespaces WHERE namespace_text = $1", str )
	};

	if ( result.size() == 0 ) co_return std::nullopt;

	co_return result[ 0 ][ 0 ].as< NamespaceID >();
}

drogon::Task< std::expected< NamespaceID, drogon::HttpResponsePtr > >
	findOrCreateNamespace( const std::string& str, drogon::orm::DbClientPtr db )
{
	const auto id_search { co_await searchNamespace( str, db ) };
	if ( id_search.has_value() )
	{
		co_return id_search.value();
	}

	const auto id_creation { co_await db->execSqlCoro(
		"INSERT INTO tag_namespaces (namespace_text) VALUES ($1) ON CONFLICT DO NOTHING RETURNING namespace_id",
		str ) };

	if ( id_creation.size() > 0 ) co_return id_creation[ 0 ][ 0 ].as< NamespaceID >();

	co_return std::unexpected( createInternalError( "Failed to create namespace: {}", str ) );
}

} // namespace idhan