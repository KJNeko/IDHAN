#pragma once

namespace idhan::db
{
//! Session membership is authoritative, including imports from the current run.
inline constexpr char subscriptionRecordsCte[] {
	//language=postgresql
	R"SQL(
		WITH subscription_records AS (
		    SELECT
		        s.subscription_id,
		        s.subscription_group_id,
		        r.record_id
		    FROM subscriptions s
		        JOIN download_session_records r ON r.download_session_id = s.session_id
		),
		subscription_record_stats AS (
		        SELECT
		        r.subscription_id,
		        count(*) AS record_count,
		        COALESCE(sum(f.size), 0)::bigint AS total_size_bytes
		    FROM subscription_records r
		        LEFT JOIN file_info f USING (record_id)
		    GROUP BY r.subscription_id
		),
		group_records AS (
		    SELECT DISTINCT subscription_group_id, record_id
		    FROM subscription_records
		),
		group_record_stats AS (
		    SELECT
		        r.subscription_group_id,
		        count(*) AS record_count,
		        COALESCE(sum(f.size), 0)::bigint AS total_size_bytes
		    FROM group_records r
		        LEFT JOIN file_info f USING (record_id)
		    GROUP BY r.subscription_group_id
		)
	)SQL"
};
} // namespace idhan::db
