#pragma once

#include "db/subscriptions.hpp"

namespace idhan::subscriptions
{
//! Fields needed by the subscription list; download history stays on the session.
struct SubscriptionListItem
{
	SubscriptionID id {};
	SubscriptionGroupID group_id {};
	std::map< std::string, std::string > parameters {};
	bool enabled { true };
	std::int64_t next_run_at {};
	std::optional< DownloadSessionID > session_id {};
	std::optional< DownloadSessionID > active_session_id {};
	std::optional< std::string > last_error {};
	bool dead {};
	std::int64_t record_count {};
	std::int64_t imported_count {};
	std::int64_t total_size_bytes {};
};

[[nodiscard]] inline SubscriptionListItem subscriptionListItem( db::Subscription item )
{
	return { item.id,          item.group_id,     std::move( item.parameters ), item.enabled,
	         item.next_run_at, item.session_id,   item.active_session_id,       std::move( item.last_error ),
	         item.dead,        item.record_count, item.imported_count,          item.total_size_bytes };
}
} // namespace idhan::subscriptions
