#pragma once

namespace idhan::db
{
// Include records already discovered in active runs before those runs are committed to the log.
inline constexpr char subscriptionRecordsCte[] {
	"WITH RECURSIVE active_urls AS ("
	"SELECT s.subscription_id,u.download_session_id,u.download_session_url_id,u.record_id "
	"FROM subscriptions s JOIN download_session_urls u ON u.download_session_id=s.active_session_id "
	"AND u.download_session_url_id=s.active_root_id UNION ALL "
	"SELECT p.subscription_id,c.download_session_id,c.download_session_url_id,c.record_id "
	"FROM active_urls p JOIN download_session_urls c ON c.download_session_id=p.download_session_id "
	"AND c.parent_url_id=p.download_session_url_id), "
	"subscription_records AS (SELECT subscription_id,record_id FROM subscriptions_log UNION "
	"SELECT subscription_id,record_id FROM active_urls WHERE record_id IS NOT NULL) "
};
} // namespace idhan::db
