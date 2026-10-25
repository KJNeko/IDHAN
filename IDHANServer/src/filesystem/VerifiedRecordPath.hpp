#pragma once

#include "IDHANTypes.hpp"
#include "clusters/ClusterAccess.hpp"

namespace idhan::filesystem
{
class VerifiedRecordPath
{
	ClusterAccess m_cluster;
	std::filesystem::path m_relative;
	ClusterID m_cluster_id;

  public:

	VerifiedRecordPath( ClusterAccess cluster, std::filesystem::path relative, const ClusterID cluster_id ) :
	  m_cluster( std::move( cluster ) ),
	  m_relative( std::move( relative ) ),
	  m_cluster_id( cluster_id )
	{}

	[[nodiscard]] std::filesystem::path path() const { return m_cluster.root() / m_relative; }

	[[nodiscard]] std::expected< void, std::string > verify() const { return m_cluster.verify(); }

	[[nodiscard]] ClusterID clusterId() const { return m_cluster_id; }

	// Borrowed access remains valid only while this verified path is alive and has not been moved.
	[[nodiscard]] const ClusterAccess& cluster() const { return m_cluster; }
};
} // namespace idhan::filesystem
