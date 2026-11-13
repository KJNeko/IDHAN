#pragma once

#include <folly/Executor.h>
#include <folly/coro/Mutex.h>

#include "SubscriptionEventState.hpp"
#include "SubscriptionUpdateQueue.hpp"
#include "idhan/http/streaming/SSEEventHub.hpp"

namespace idhan::subscriptions
{
class SubscriptionEventHub
{
	//! Serializes asynchronous reads, cache changes and baseline installation; never hold a thread mutex over I/O.
	folly::coro::Mutex m_mutex {};
	SubscriptionEventState m_state {};
	SubscriptionUpdateQueue m_pending {};

	runtime::Task<> refreshLocked( db::DBContext& db, std::optional< SubscriptionGroupID > group_id );

	runtime::Task<> refreshSessions( db::DBContext& db, std::vector< DownloadSessionID > session_ids );

  public:

	static SubscriptionEventHub& instance();
	void sessionRecordsChanged( DownloadSessionID session_id );
	void runUpdates( db::DBContext& db, folly::Executor::KeepAlive<> executor, std::stop_token stop );

	//! The scheduler reconciles all rows; commands can limit the read to the affected group.
	runtime::Task<> refresh( db::DBContext& db, std::optional< SubscriptionGroupID > group_id = std::nullopt );
	runtime::Task<> initializeSubscription( db::DBContext& db, http::SSESubscription& subscription );
};
} // namespace idhan::subscriptions
