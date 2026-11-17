#pragma once

#include <folly/Executor.h>

#include <thread>

#include "jobs/BackgroundJob.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::maintenance
{
class BandwidthMaintenance
{
	db::DBContext& m_db;
	folly::Executor::KeepAlive<> m_executor;
	jobs::BackgroundJob m_job { "Bandwidth log persistence", "bandwidth" };
	std::jthread m_thread;

	void run( std::stop_token stop );

  public:

	BandwidthMaintenance( db::DBContext& db, folly::Executor::KeepAlive<> executor );
	~BandwidthMaintenance();
	BandwidthMaintenance( const BandwidthMaintenance& ) = delete;
	BandwidthMaintenance& operator=( const BandwidthMaintenance& ) = delete;
};
} // namespace idhan::maintenance
