//
// Created by kj16609 on 3/13/25.
//

#include "subtags.hpp"

#include "api/helpers/createBadRequest.hpp"

namespace idhan
{

drogon::Task< std::optional< SubtagID > > searchSubtag( const std::string& str, const drogon::orm::DbClientPtr db )
{
	const auto result { co_await db->execSqlCoro( "SELECT subtag_id FROM tag_subtags WHERE subtag_text = $1", str ) };

	if ( result.size() == 0 ) co_return std::nullopt;

	co_return result[ 0 ][ 0 ].as< SubtagID >();
}

drogon::Task< std::expected< SubtagID, drogon::HttpResponsePtr > >
	findOrCreateSubtag( const std::string& str, const drogon::orm::DbClientPtr db )
{
	SubtagID subtag_id { 0 };

	do {
		if ( const auto id_search = co_await searchSubtag( str, db ); id_search.has_value() )
		{
			co_return id_search.value();
		}

		const auto id_creation { co_await db->execSqlCoro(
			"INSERT INTO tag_subtags (subtag_text) VALUES ($1) ON CONFLICT DO NOTHING RETURNING subtag_id", str ) };

		if ( id_creation.size() > 0 ) subtag_id = id_creation[ 0 ][ 0 ].as< SubtagID >();
	}
	while ( subtag_id == 0 );

	co_return subtag_id;
}

} // namespace idhan