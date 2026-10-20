#pragma once

#include "clusters/ClusterAccess.hpp"

namespace idhan::filesystem
{
class VerifiedRecordPath
{
	ClusterAccess m_cluster;
	std::filesystem::path m_relative;

  public:

	VerifiedRecordPath( ClusterAccess cluster, std::filesystem::path relative ) :
	  m_cluster( std::move( cluster ) ),
	  m_relative( std::move( relative ) )
	{}

	[[nodiscard]] std::filesystem::path path() const { return m_cluster.root() / m_relative; }

	[[nodiscard]] std::expected< void, std::string > verify() const { return m_cluster.verify(); }
};
} // namespace idhan::filesystem
