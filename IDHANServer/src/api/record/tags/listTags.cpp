//
// Created by kj16609 on 3/11/25.
//

#include "api/RecordAPI.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > RecordAPI::
	listTags( [[maybe_unused]] const drogon::HttpRequestPtr request, const RecordID record_id )
{
	const auto db { drogon::app().getDbClient() };

	const auto result {
		co_await db->execSqlCoro( "SELECT tag_domain_id, tag_id FROM tag_mappings WHERE record_id = $1", record_id )
	};

	Json::Value json {};

	std::unordered_map< TagDomainID, std::vector< TagID > > domain_map {};

	for ( const auto& row : result )
	{
		const auto tag_domain_id { row[ 0 ].as< TagDomainID >() };
		const auto tag_id { row[ 1 ].as< TagID >() };

		if ( auto itter = domain_map.find( tag_domain_id ); itter != domain_map.end() )
		{
			itter->second.emplace_back( tag_id );
		}
		else
		{
			domain_map.insert_or_assign( tag_domain_id, std::vector< TagID > { tag_id } );
		}
	}

	for ( const auto& [ domain_id, tag_ids ] : domain_map )
	{
		Json::Value obj {};
		obj[ "tag_domain_id" ] = static_cast< Json::Value::LargestUInt >( domain_id );

		{
			Json::Value tag_ids_json {};
			Json::ArrayIndex index { 0 };
			for ( const auto& tag_id : tag_ids )
			{
				tag_ids_json[ index++ ] = static_cast< Json::Value::LargestUInt >( tag_id );
			}

			obj[ "tag_ids" ] = std::move( tag_ids_json );
		}

		json.append( obj );
	}

	co_return drogon::HttpResponse::newHttpJsonResponse( json );
}

} // namespace idhan::api
