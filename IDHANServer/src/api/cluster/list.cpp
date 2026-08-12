#include "api/ClusterAPI.hpp"

namespace idhan::api
{

ClusterAPI::ResponseTask ClusterAPI::list( [[maybe_unused]] drogon::HttpRequestPtr request )
{
	auto db { drogon::app().getDbClient() };
	const auto result { co_await db->execSqlCoro( "SELECT cluster_id FROM file_clusters" ) };

	Json::Value root { Json::arrayValue };

	for ( Json::ArrayIndex i = 0; i < result.size(); ++i )
	{
		const ClusterID id { result[ i ][ 0 ].as< ClusterID >() };

		const auto info { co_await getInfo( id, db ) };

		if ( !info ) co_return info.error();

		root[ i ] = info.value();
	}

	co_return drogon::HttpResponse::newHttpJsonResponse( root );
}

} // namespace idhan::api
