//
// Created by kj16609 on 11/18/24.
//

#include <expected>

#include "api/ClusterAPI.hpp"
#include "logging/log.hpp"

namespace idhan::api
{

ClusterAPI::ResponseTask ClusterAPI::info( drogon::HttpRequestPtr request, const ClusterID cluster_id )
{
	auto db { drogon::app().getDbClient() };

	co_return co_await infoT( request, cluster_id, db );
}

drogon::Task< std::expected< Json::Value, drogon::HttpResponsePtr > > getInfo(
	ClusterID cluster_id,
	const DbClientPtr transaction )
{
	const auto cluster_info { co_await transaction->execSqlCoro(
		"SELECT cluster_id, ratio_number, size_used, size_limit, file_count, read_only, allowed_thumbnails, allowed_files, "
		"cluster_name, folder_path FROM file_clusters WHERE cluster_id = $1",
		cluster_id ) };

	if ( cluster_info.empty() )
	{
		log::warn( "Cluster info could not be found for cluster id: {}", cluster_id );
		co_return std::unexpected(
			drogon::HttpResponse::newHttpResponse( drogon::k404NotFound, drogon::CT_TEXT_HTML ) );
	}
	log::debug( "Found info for cluster {}", cluster_id );

	Json::Value json;

	json[ "cluster_id" ] = static_cast< Json::Value::UInt >( cluster_id );
	json[ "readonly" ] = cluster_info[ 0 ][ "read_only" ].as< bool >();
	json[ "name" ] = cluster_info[ 0 ][ "cluster_name" ].as< std::string >();
	const auto size_used { cluster_info[ 0 ][ "size_used" ].as< std::int64_t >() };
	const auto size_limit { cluster_info[ 0 ][ "size_limit" ].as< std::int64_t >() };
	json[ "size" ][ "used" ] = size_used;
	json[ "size" ][ "limit" ] = size_limit;
	// json[ "size" ][ "available" ] = cluster_info[ 0 ][ "available" ].as< int >();
	if ( size_limit == 0 ) // TODO: Switch this to use the available size as the size remaining in the disk partition
		json[ "size" ][ "available" ] = std::numeric_limits< std::int64_t >::max();
	else
		json[ "size" ][ "available" ] = cluster_info[ 0 ][ "size_limit" ].as< std::int64_t >()
		                              - cluster_info[ 0 ][ "size_used" ].as< std::int64_t >();
	json[ "file_count" ] = cluster_info[ 0 ][ "file_count" ].as< std::size_t >();
	json[ "ratio_number" ] = cluster_info[ 0 ][ "ratio_number" ].as< std::size_t >();
	json[ "path" ] = cluster_info[ 0 ][ "folder_path" ].as< std::string >();

	// TODO: Type

	log::debug( "Populated json" );

	co_return json;
}

ClusterAPI::ResponseTask ClusterAPI::infoT(
	[[maybe_unused]] drogon::HttpRequestPtr request,
	const ClusterID cluster_id,
	const DbClientPtr transaction )
{
	const auto result { co_await getInfo( cluster_id, transaction ) };

	if ( !result ) co_return result.error();

	co_return drogon::HttpResponse::newHttpJsonResponse( result.value() );
}

} // namespace idhan::api
