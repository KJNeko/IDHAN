//
// Created by kj16609 on 11/18/24.
//

#include "api/ClusterAPI.hpp"

namespace idhan::api
{

ClusterAPI::ResponseTask ClusterAPI::list( drogon::HttpRequestPtr request )
{
	auto db { drogon::app().getDbClient() };
	const auto result { co_await db->execSqlCoro( "SELECT cluster_id FROM file_clusters" ) };

	Json::Value root {};

	for ( Json::ArrayIndex i = 0; i < result.size(); ++i )
	{
		root[ i ] = result[ static_cast< std::size_t >( i ) ][ 0 ].as< ClusterID >();
	}

	co_return drogon::HttpResponse::newHttpJsonResponse( root );
}

} // namespace idhan::api