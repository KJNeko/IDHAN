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

// Scheduled runs admitted per group; manually requested runs are not counted against it.
constexpr Int max_running_per_group { 4 };

struct Subscription
{
	SubscriptionID id {};
	SubscriptionGroupID group_id {};
	std::string tags {};
	bool enabled {};
	std::int64_t next_run_at {};
	bool initial_sync_complete {};
	std::optional< DownloadSessionID > active_session_id {};
	std::optional< DownloadSessionID > last_session_id {};
	std::optional< std::int64_t > last_finished_at {};
	std::optional< std::string > last_error {};
	std::optional< std::int64_t > latest_post_at {};
	bool post_dates_complete {};
	bool dead {};
	std::int64_t record_count {};
	std::map< std::string, std::string > parameters {};
	std::optional< DownloadSessionID > session_id {};
	std::optional< DownloadSessionUrlID > active_root_id {};
	std::int64_t imported_count {};
	std::int64_t known_count {};
	std::int64_t total_size_bytes {};
};

struct SubscriptionRunResult
{
	SubscriptionID subscription_id {};
	std::optional< DownloadSessionID > session_id {};
	std::optional< DownloadSessionUrlID > root_id {};
	std::int64_t imported_count {};
	std::int64_t known_count {};
};

[[nodiscard]] runtime::Task< SubscriptionRunResult > subscriptionRunResult( DBContext& db, SubscriptionID id );
[[nodiscard]] runtime::Task< std::vector< RecordID > > subscriptionRunRecords( DBContext& db, SubscriptionID id );
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
	std::int64_t record_count {};
	std::int64_t running_count {};
	std::int64_t dead_count {};
	std::int64_t error_count {};
	std::string origin {};
	std::int64_t total_size_bytes {};
};

[[nodiscard]] runtime::Task< std::vector< SubscriptionGroup > > subscriptionGroups( DBContext& db );
[[nodiscard]] runtime::Task< std::optional< SubscriptionGroup > > findSubscriptionGroup(
	DBContext& db,
	SubscriptionGroupID id );
[[nodiscard]] runtime::Task< SubscriptionGroupID > createSubscriptionGroup( DBContext& db, SubscriptionGroup group );
[[nodiscard]] runtime::Task< bool > updateSubscriptionGroup( DBContext& db, SubscriptionGroup group );
[[nodiscard]] runtime::Task< bool > deleteSubscriptionGroup( DBContext& db, SubscriptionGroupID id );
[[nodiscard]] runtime::Task< bool > requestSubscriptionGroupRun( DBContext& db, SubscriptionGroupID id );

struct SubscriptionRun
{
	SubscriptionID id {};
	DownloadSessionID session_id {};
	std::string url;
	std::string settings;
	DownloadSessionUrlID root_id { 1 };
	bool recovering {};
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
// Claim and persist a root atomically; startup download recovery can resume it after a crash.
[[nodiscard]] runtime::Task< std::optional< SubscriptionRun > > claimSubscription( DBContext& db );
// Called before the scheduler starts; interrupted runs wait for admission like new runs.
[[nodiscard]] runtime::Task<> queueInterruptedSubscriptions( DBContext& db );
[[nodiscard]] runtime::Task< std::vector< DownloadSessionID > > subscriptionSessionIds( DBContext& db );
[[nodiscard]] runtime::Task< std::vector< SubscriptionGroupID > > subscriptionGroupsForSessions(
	DBContext& db,
	std::vector< DownloadSessionID > session_ids );
[[nodiscard]] runtime::Task<> refreshSubscriptionActivity( DBContext& db );
[[nodiscard]] runtime::Task<> finishSubscription( DBContext& db, SubscriptionID id, DownloadSessionID session_id );
} // namespace idhan::db
