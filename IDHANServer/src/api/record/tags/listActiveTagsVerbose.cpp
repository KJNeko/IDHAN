//
// Created by kj16609 on 6/12/25.
//

#include <unordered_map>
#include <vector>

#include "api/RecordAPI.hpp"
#include "api/helpers/createBadRequest.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > RecordAPI::listActiveTagsVerbose(
	[[maybe_unused]] const drogon::HttpRequestPtr request,
	const RecordID record_id )
{
	const auto db { drogon::app().getDbClient() };

	const auto result { co_await db->execSqlCoro(
		R"(
			SELECT COALESCE(ideal_tag_id, tag_id) AS effective_tag_id, tag_domain_id,
			       tag_id AS original_tag_id, NULL::INTEGER AS origin_id
			FROM active_tag_mappings
			WHERE record_id = $1
			UNION ALL
			SELECT tag_id, tag_domain_id,
			       NULL::INTEGER AS original_tag_id, origin_id
			FROM active_tag_mappings_parents
			WHERE record_id = $1
		)",
		record_id ) };

	struct TagProvenance
	{
		bool is_explicit { false };
		std::vector< TagID > aliased_from;
		std::vector< TagID > inherited_from;
	};

	std::unordered_map< TagDomainID, std::unordered_map< TagID, TagProvenance > > provenance_map;

	for ( const auto& row : result )
	{
		const auto effective_tag_id { row[ 0 ].as< TagID >() };
		const auto tag_domain_id { row[ 1 ].as< TagDomainID >() };
		const auto original_tag_id { row[ 2 ].isNull() ? TagID { 0 } : row[ 2 ].as< TagID >() };
		const auto origin_id { row[ 3 ].isNull() ? TagID { 0 } : row[ 3 ].as< TagID >() };

		auto& prov { provenance_map[ tag_domain_id ][ effective_tag_id ] };

		if ( origin_id != 0 )
		{
			prov.inherited_from.push_back( origin_id );
		}
		else if ( original_tag_id != 0 )
		{
			if ( original_tag_id == effective_tag_id )
				prov.is_explicit = true;
			else
				prov.aliased_from.push_back( original_tag_id );
		}
	}

	Json::Value json { Json::arrayValue };

	for ( const auto& [ domain_id, tag_map ] : provenance_map )
	{
		for ( const auto& [ tag_id, prov ] : tag_map )
		{
			Json::Value obj {};
			obj[ "tag_id" ] = static_cast< Json::Value::LargestUInt >( tag_id );
			obj[ "tag_domain_id" ] = static_cast< Json::Value::LargestUInt >( domain_id );
			obj[ "explicit" ] = prov.is_explicit;

			{
				Json::Value arr { Json::arrayValue };
				for ( const auto id : prov.aliased_from ) arr.append( static_cast< Json::Value::LargestUInt >( id ) );
				obj[ "aliased_from" ] = std::move( arr );
			}

			{
				Json::Value arr { Json::arrayValue };
				for ( const auto id : prov.inherited_from ) arr.append( static_cast< Json::Value::LargestUInt >( id ) );
				obj[ "inherited_from" ] = std::move( arr );
			}

			json.append( std::move( obj ) );
		}
	}

	co_return drogon::HttpResponse::newHttpJsonResponse( json );
}

} // namespace idhan::api
