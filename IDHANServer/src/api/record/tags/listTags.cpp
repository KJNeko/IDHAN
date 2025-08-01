//
// Created by kj16609 on 3/11/25.
//

#include "api/RecordAPI.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > RecordAPI::
	listTags( const drogon::HttpRequestPtr request, const RecordID record_id )
{
	const auto db { drogon::app().getDbClient() };

	const auto result {
		co_await db->execSqlCoro( "SELECT domain_id, tag_id FROM tag_mappings WHERE record_id = $1", record_id )
	};

	Json::Value json {};

	std::unordered_map< TagDomainID, Json::ArrayIndex > domain_map {};

	for ( const auto& row : result )
	{
		const auto domain_id { row[ 0 ].as< TagDomainID >() };
		const auto tag_id { row[ 1 ].as< TagID >() };

		if ( auto itter = domain_map.find( domain_id ); itter != domain_map.end() )
		{
			json[ itter->second ][ "tag_ids" ].append( tag_id );
			continue;
		}
		else
		{
			Json::Value obj {};
			obj[ "domain_id" ] = static_cast< Json::Value::UInt >( domain_id );
			obj[ "tag_ids" ] = Json::arrayValue;
			obj[ "tag_ids" ].append( tag_id );
			json.append( obj );

			domain_map[ domain_id ] = json.size() - 1;
		}
	}

	co_return drogon::HttpResponse::newHttpJsonResponse( json );
}

} // namespace idhan::api