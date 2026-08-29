#include "api/ClusterAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "db/commitTransaction.hpp"
#include "filesystem/clusters/ClusterManager.hpp"

namespace idhan::api
{

ClusterAPI::ResponseTask ClusterAPI::remove(
	[[maybe_unused]] drogon::HttpRequestPtr request,
	const ClusterID cluster_id )
{
	const auto db { drogon::app().getDbClient() };
	auto transaction { co_await db->newTransactionCoro() };
	const bool removed { co_await filesystem::ClusterManager::getInstance().removeCluster( cluster_id, transaction ) };
	const bool committed { co_await commitTransaction( std::move( transaction ) ) };

	if ( !committed ) co_return createInternalError( "Failed to commit removal of cluster {}", cluster_id );

	if ( !removed ) co_return createNotFound( "Cluster {} does not exist", cluster_id );

	co_await filesystem::ClusterManager::getInstance().reloadClusters( db );

	co_return drogon::HttpResponse::newHttpJsonResponse( Json::Value() );
}

} // namespace idhan::api
