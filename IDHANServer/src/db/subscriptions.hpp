#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "IDHANTypes.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBContext;
class DBTransaction;

struct Subscription
{
	SubscriptionID id {};
	SubscriptionGroupID group_id {};
	std::string tags {};
	bool enabled {};
	std::int64_t next_run_at {};
	std::optional< DownloadSessionID > active_session_id {};
	std::optional< DownloadSessionID > last_session_id {};
	std::optional< std::int64_t > last_finished_at {};
	std::optional< std::string > last_error {};
	std::map< std::string, std::string > parameters {};
	std::optional< DownloadSessionID > session_id {};
	bool run_requested {};
};

struct SubscriptionRunResult
{
	SubscriptionID subscription_id {};
	std::optional< DownloadSessionID > session_id {};
};

[[nodiscard]] runtime::Task< std::vector< SubscriptionID > > createSubscriptions(
	DBContext& db,
	SubscriptionGroupID group_id,
	std::vector< Subscription > items );

struct SubscriptionGroup
{
	SubscriptionGroupID id {};
	std::string name {};
	std::string generator { "gelbooru:search" };
	std::int64_t interval_seconds { 86400 };
	bool enabled { true };
	std::int64_t subscription_count {};
	std::int64_t running_count {};
	std::int64_t error_count {};
	std::string origin {};
};

[[nodiscard]] runtime::Task< std::vector< SubscriptionGroup > > subscriptionGroups( DBContext& db );
[[nodiscard]] runtime::Task< std::optional< SubscriptionGroup > > findSubscriptionGroup(
	DBContext& db,
	SubscriptionGroupID id );
[[nodiscard]] runtime::Task< SubscriptionGroupID > createSubscriptionGroup( DBContext& db, SubscriptionGroup group );
[[nodiscard]] runtime::Task< bool > updateSubscriptionGroup( DBContext& db, SubscriptionGroup group );
[[nodiscard]] runtime::Task< bool > deleteSubscriptionGroup( DBContext& db, SubscriptionGroupID id );
[[nodiscard]] runtime::Task< bool > requestSubscriptionGroupRun( DBContext& db, SubscriptionGroupID id );

//! The caller retains the transaction row locks until activation.
struct ScheduledSubscription
{
	SubscriptionID id {};
	std::map< std::string, std::string > parameters {};
	std::string generator;
	std::string origin;
	std::optional< DownloadSessionID > session_id {};
};

[[nodiscard]] runtime::Task< std::vector< Subscription > > subscriptions( DBContext& db );
[[nodiscard]] runtime::Task< std::vector< Subscription > > subscriptionsInGroup(
	DBContext& db,
	SubscriptionGroupID group_id );
[[nodiscard]] runtime::Task< std::optional< Subscription > > findSubscription( DBContext& db, SubscriptionID id );
[[nodiscard]] runtime::Task< std::optional< SubscriptionID > > createSubscription(
	DBContext& db,
	Subscription subscription );
[[nodiscard]] runtime::Task< bool > updateSubscription( DBContext& db, Subscription subscription );
[[nodiscard]] runtime::Task< bool > requestSubscriptionRun( DBContext& db, SubscriptionID id );
[[nodiscard]] runtime::Task< bool > deleteSubscription( DBContext& db, SubscriptionID id );
//! The caller must activate the check before committing the transaction.
[[nodiscard]] runtime::Task< std::optional< ScheduledSubscription > > nextScheduledSubscription(
	DBTransaction& transaction,
	SubscriptionGroupID group_id );
[[nodiscard]] runtime::Task<> startSubscription(
	DBTransaction& transaction,
	SubscriptionID id,
	DownloadSessionID session_id );
//! Interrupted checks are scheduled afresh; their previous downloader sessions remain available.
[[nodiscard]] runtime::Task<> queueInterruptedSubscriptions( DBContext& db );
[[nodiscard]] runtime::Task< bool > finishSubscription(
	DBContext& db,
	SubscriptionID id,
	DownloadSessionID session_id,
	std::optional< std::string > error );
} // namespace idhan::db
