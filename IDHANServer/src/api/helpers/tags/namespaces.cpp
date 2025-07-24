//
// Created by kj16609 on 3/13/25.
//

#include "namespaces.hpp"

#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"
#include "logging/log.hpp"

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
	NamespaceID namespace_id { 0 };
	std::size_t counter { 0 };

	do {
		if ( counter > 128 ) co_return std::unexpected( createBadRequest( "Too many namespace creation attempts" ) );
		++counter;

		if ( const auto search_result { co_await searchNamespace( str, db ) }; search_result.has_value() )
			co_return search_result.value();

		const auto id_creation { co_await db->execSqlCoro(
			"INSERT INTO tag_namespaces (namespace_text) VALUES ($1) ON CONFLICT DO NOTHING RETURNING namespace_id",
			str ) };

		if ( id_creation.size() > 0 ) namespace_id = id_creation[ 0 ][ 0 ].as< NamespaceID >();
	}
	while ( namespace_id == 0 );

	co_return namespace_id;
}

} // namespace idhan