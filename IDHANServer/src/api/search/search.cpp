//
// Created by kj16609 on 3/22/25.
//

#include "api/IDHANSearchAPI.hpp"
#include "api/helpers/getArrayParameters.hpp"
#include "db/TagSearch.hpp"
#include "logging/log.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > IDHANSearchAPI::search( drogon::HttpRequestPtr request )
{
	auto db { drogon::app().getDbClient() };

	// Drogon does not support tag_id=1?tag_id=2 for some reason, But it's possible to be sent like that, So we'll handle it here.
	const auto tag_ids { parseArrayParmeters< std::size_t >( request, "tag_id" ) };
	const auto domain_ids { parseArrayParmeters< TagDomainID >( request, "tag_domains" ) };

	/*
	for ( TagDomainID domain_id : domain_ids )
	{
		TagSearch searcher { domain_id, db };

		for ( const TagID& tag_id : tag_ids )
		{
			auto result { co_await searcher.addID( tag_id ) };

			if ( !result.has_value() ) co_return result.error();
		}

		const auto records { co_await searcher.search() };

		if ( !records.has_value() ) co_return records.error();
	}
	*/

	const auto result { co_await db->execSqlCoro(
		"SELECT record_id FROM tag_mappings WHERE tag_id = $1", static_cast< TagID >( tag_ids[ 0 ] ) ) };

	if ( result.empty() )
	{
		Json::Value root {};
		root[ "file_ids" ] = Json::arrayValue;
		co_return drogon::HttpResponse::newHttpJsonResponse( root );
	}
	else
	{
		Json::Value file_ids {};

		for ( const auto& row : result )
		{
			const auto id { row[ 0 ].as< std::size_t >() };
			file_ids.append( id );
		}

		Json::Value json {};
		json[ "file_ids" ] = std::move( file_ids );

		co_return drogon::HttpResponse::newHttpJsonResponse( json );
	}
}

} // namespace idhan::api