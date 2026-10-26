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
class TagCountMaintenance
{
	db::DBContext& m_db;
	folly::Executor::KeepAlive<> m_executor;
	jobs::BackgroundJob m_job { "Tag count accumulation", "tag_counts" };
	std::jthread m_thread;

	void run( std::stop_token stop );

  public:

	TagCountMaintenance( db::DBContext& db, folly::Executor::KeepAlive<> executor );
	~TagCountMaintenance();
	TagCountMaintenance( const TagCountMaintenance& ) = delete;
	TagCountMaintenance& operator=( const TagCountMaintenance& ) = delete;
};
} // namespace idhan::maintenance
