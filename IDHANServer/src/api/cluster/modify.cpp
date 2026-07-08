//
// Created by kj16609 on 11/18/24.
//

#include "api/ClusterAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "logging/log.hpp"

namespace idhan::api
{

ClusterAPI::ResponseTask ClusterAPI::modifyT(
	const drogon::HttpRequestPtr request,
	const ClusterID cluster_id,
	const DbClientPtr transaction )
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

	if ( json_ptr == nullptr ) co_return createBadRequest( "No json data supplied" );

	const auto& json { *json_ptr };

	// operator[] on a non-object root throws Json::LogicError, which would surface as a 500
	if ( !json.isObject() ) co_return createBadRequest( "Invalid json object. Expected object as root item" );

	if ( json[ "readonly" ].isBool() )
	{
		co_await transaction->execSqlCoro(
			"UPDATE file_clusters SET read_only = $1 WHERE cluster_id = $2", json[ "readonly" ].asBool(), cluster_id );
	}

	if ( json[ "name" ].isString() )
	{
		// cluster_name is UNIQUE and the PG backend throws no typed unique-violation
		// error, so pre-check the rename target instead of letting the UPDATE 500
		const auto name_search { co_await transaction->execSqlCoro(
			"SELECT cluster_id FROM file_clusters WHERE cluster_name = $1 AND cluster_id != $2",
			json[ "name" ].asString(),
			cluster_id ) };

		if ( !name_search.empty() )
			co_return createConflict( "A cluster with the name {} already exists", json[ "name" ].asString() );

		co_await transaction->execSqlCoro(
			"UPDATE file_clusters SET cluster_name = $1 WHERE cluster_id = $2", json[ "name" ].asString(), cluster_id );
	}

	if ( json[ "ratio" ].isInt64() )
	{
		const auto ratio { json[ "ratio" ].asInt64() };

		// ratio_number is a SMALLINT; anything outside its range would silently wrap
		if ( ratio < 0 || ratio > std::numeric_limits< std::int16_t >::max() )
			co_return createBadRequest(
				"ratio must be between 0 and {}, got {}", std::numeric_limits< std::int16_t >::max(), ratio );

		co_await transaction->execSqlCoro(
			"UPDATE file_clusters SET ratio_number = $1 WHERE cluster_id = $2",
			static_cast< std::int16_t >( ratio ),
			cluster_id );
	}

	if ( json[ "size" ].isObject() && json[ "size" ][ "limit" ].isIntegral() )
	{
		const auto size_limit { json[ "size" ][ "limit" ].asInt64() };

		if ( size_limit < 0 ) co_return createBadRequest( "size limit cannot be negative, got {}", size_limit );

		co_await transaction->execSqlCoro(
			"UPDATE file_clusters SET size_limit = $1 WHERE cluster_id = $2", size_limit, cluster_id );
	}

	co_return co_await infoT( request, cluster_id, transaction );
}

ClusterAPI::ResponseTask ClusterAPI::modify( drogon::HttpRequestPtr request, const ClusterID cluster_id )
{
	const auto db { drogon::app().getDbClient() };
	const auto transaction { co_await db->newTransactionCoro() };
	co_return co_await modifyT( request, cluster_id, transaction );
}

} // namespace idhan::api
