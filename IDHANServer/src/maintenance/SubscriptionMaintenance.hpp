#pragma once

#include <folly/Executor.h>

#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>

#include "jobs/BackgroundJob.hpp"
#include "jobs/SubscriptionJob.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::maintenance
{
class SubscriptionMaintenance
{
	db::DBContext& m_db;
	folly::Executor::KeepAlive<> m_executor;
	jobs::BackgroundJob m_job { "Subscription scheduler", "subscriptions" };
	std::map< SubscriptionID, std::unique_ptr< jobs::SubscriptionJob > > m_runs {};
	std::jthread m_thread;
	inline static std::mutex s_wake_mutex {};
	inline static std::condition_variable_any s_wake {};
	inline static bool s_woken { false };
	void run( std::stop_token stop );
	runtime::Task<> pollSubscriptions();

  public:

	static void wake();

	SubscriptionMaintenance( db::DBContext& db, folly::Executor::KeepAlive<> executor );
	~SubscriptionMaintenance();
	SubscriptionMaintenance( const SubscriptionMaintenance& ) = delete;
	SubscriptionMaintenance& operator=( const SubscriptionMaintenance& ) = delete;
};
} // namespace idhan::maintenance
