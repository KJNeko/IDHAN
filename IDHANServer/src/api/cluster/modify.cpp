//
// Created by kj16609 on 11/18/24.
//

#include "api/ClusterAPI.hpp"
#include "logging/log.hpp"

namespace idhan::api
{

ClusterAPI::ResponseTask ClusterAPI::modify( drogon::HttpRequestPtr request, const ClusterID cluster_id )
{
	auto db { drogon::app().getDbClient() };
	auto transaction { co_await db->newTransactionCoro() };
	co_return co_await modifyT( request, cluster_id, transaction );
}

ClusterAPI::ResponseTask ClusterAPI::modifyT(
	drogon::HttpRequestPtr request,
	const ClusterID cluster_id,
	DbClientPtr transaction )
{
	log::debug( "Modifying cluster: {}", cluster_id );
	const auto cluster_info {
		co_await transaction->execSqlCoro( "SELECT cluster_id FROM file_clusters WHERE cluster_id = $1", cluster_id )
	};

	if ( cluster_info.empty() )
	{
		log::warn( "Cluster id {} could not be found", cluster_id );
		co_return drogon::HttpResponse::newHttpResponse( drogon::k404NotFound, drogon::CT_TEXT_HTML );
	}

	log::debug( "Found cluster for id {}", cluster_id );

	const auto json_ptr { request->getJsonObject() };

	if ( json_ptr == nullptr ) throw std::runtime_error( "No json object found" );

	const auto& json { *json_ptr };

	if ( json[ "readonly" ].isBool() )
	{
		co_await transaction->execSqlCoro(
			"UPDATE file_clusters SET read_only = $1 WHERE cluster_id = $2", json[ "readonly" ].asBool(), cluster_id );
	}

	if ( json[ "name" ].isString() )
	{
		co_await transaction->execSqlCoro(
			"UPDATE file_clusters SET cluster_name = $1 WHERE cluster_id = $2", json[ "name" ].asString(), cluster_id );
	}

	if ( json[ "ratio" ].isInt64() )
	{
		co_await transaction->execSqlCoro(
			"UPDATE file_clusters SET ratio_number = $1 WHERE cluster_id = $2",
			static_cast< std::uint16_t >( json[ "ratio" ].asInt64() ),
			cluster_id );
	}

	if ( json[ "size" ].isObject() && json[ "size" ][ "limit" ].isIntegral() )
	{
		co_await transaction->execSqlCoro(
			"UPDATE file_clusters SET size_limit = $1 WHERE cluster_id = $2",
			json[ "size" ][ "limit" ].asInt64(),
			cluster_id );
	}

	co_return co_await infoT( request, cluster_id, transaction );
}

} // namespace idhan::api
