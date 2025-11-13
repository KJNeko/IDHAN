//
// Created by kj16609 on 10/30/25.
//

#include "IDHANTypes.hpp"
#include "clusters/ClusterManager.hpp"
#include "threading/ExpectedTask.hpp"

namespace idhan::filesystem
{

ExpectedTask< std::filesystem::path > getClusterPath( const ClusterID cluster_id )
{
	co_return co_await ClusterManager::getInstance().getClusterPath( cluster_id );
}

} // namespace idhan::filesystem
