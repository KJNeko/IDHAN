#pragma once
#include <folly/coro/Mutex.h>

#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "ClusterInfo.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::filesystem
{
class ClusterWrite;

class ClusterManager
{
  public:

	using Configuration = std::map< ClusterID, ClusterInfo >;
	ClusterManager();
	[[nodiscard]] std::optional< ClusterInfo > find( ClusterID id ) const;
	[[nodiscard]] std::vector< ClusterInfo > list() const;
	// Configuration writers serialize through commit and cache publication.
	// Readers retain immutable snapshots and never wait for a database transaction.
	[[nodiscard]] runtime::Task< ClusterWrite > beginWrite( db::DBContext& db );
	runtime::Task<> reloadClusters( db::DBContext& db, bool recreate_missing_markers = false );
	static ClusterManager& getInstance();

  private:

	friend class ClusterWrite;
	std::atomic< std::shared_ptr< const Configuration > > m_clusters { std::make_shared< const Configuration >() };
	folly::coro::Mutex m_write_mutex {};
	inline static ClusterManager* m_instance {};
};
} // namespace idhan::filesystem
