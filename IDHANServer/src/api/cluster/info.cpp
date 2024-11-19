//
// Created by kj16609 on 11/18/24.
//

#include "api/ClusterAPI.hpp"
#include "logging/log.hpp"

namespace idhan::api
{

ClusterAPI::ResponseTask ClusterAPI::info( drogon::HttpRequestPtr request, const ClusterID cluster_id )
{
	auto db { drogon::app().getDbClient() };
	auto transaction { co_await db->newTransactionCoro() };

	co_return co_await infoT( request, cluster_id, transaction );
}

ClusterAPI::ResponseTask ClusterAPI::infoT(
	drogon::HttpRequestPtr request, ClusterID cluster_id, std::shared_ptr< drogon::orm::Transaction > transaction )
{
	const auto cluster_info { co_await transaction->execSqlCoro(
		"SELECT cluster_id, ratio_number, size_used, size_limit, file_count, read_only, allowed_thumbnails, allowed_files, cluster_name, folder_path FROM file_clusters WHERE cluster_id = $1",
		cluster_id ) };

	if ( cluster_info.empty() )
	{
		log::warn( "Cluster info could not be found for cluster id: {}", cluster_id );
		transaction->rollback();
		co_return drogon::HttpResponse::newHttpResponse( drogon::k404NotFound, drogon::CT_TEXT_HTML );
	}
	log::debug( "Found info for cluster {}", cluster_id );

	Json::Value json;

	json[ "cluster_id" ] = cluster_id;
	json[ "readonly" ] = cluster_info[ 0 ][ "read_only" ].as< bool >();
	json[ "name" ] = cluster_info[ 0 ][ "cluster_name" ].as< std::string >();
	json[ "size" ][ "used" ] = cluster_info[ 0 ][ "size_used" ].as< int >();
	json[ "size" ][ "limit" ] = cluster_info[ 0 ][ "size_limit" ].as< int >();
	// json[ "size" ][ "available" ] = cluster_info[ 0 ][ "available" ].as< int >();
	json[ "size" ][ "available" ] =
		cluster_info[ 0 ][ "size_limit" ].as< int >() - cluster_info[ 0 ][ "size_used" ].as< int >();
	json[ "file_count" ] = cluster_info[ 0 ][ "file_count" ].as< int >();
	json[ "ratio_number" ] = cluster_info[ 0 ][ "ratio_number" ].as< int >();

	//TODO: Type

	log::debug( "Populated json" );

	co_return drogon::HttpResponse::newHttpJsonResponse( json );
}

} // namespace idhan::api