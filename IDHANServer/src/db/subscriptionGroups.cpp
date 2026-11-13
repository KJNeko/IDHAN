#include "subscriptionRecordQueries.hpp"
#include "idhan/db/DBContext.hpp"
#include "idhan/db/DBTransaction.hpp"
#include "subscriptions.hpp"

namespace idhan::db
{
static runtime::Task< std::vector< SubscriptionGroup > > loadSubscriptionGroups(
	DBContext& db,
	const SubscriptionGroupID id )
{
	DBBindings bindings;
	bindings.bind( id );
	const auto rows {
		co_await db
			.query(
				std::string { subscriptionRecordsCte } +
				"SELECT subscription_group_id, name, generator, interval_seconds, enabled, "
				"(SELECT count(*) FROM subscriptions s WHERE s.subscription_group_id=g.subscription_group_id), "
				"(SELECT count(DISTINCT log.record_id) FROM subscriptions s JOIN subscription_records log USING (subscription_id) "
				"WHERE s.subscription_group_id=g.subscription_group_id), "
				"(SELECT count(*) FROM subscriptions s WHERE s.subscription_group_id=g.subscription_group_id AND active_session_id IS NOT NULL), "
				"(SELECT count(*) FROM subscriptions s WHERE s.subscription_group_id=g.subscription_group_id AND dead), "
				"(SELECT count(*) FROM subscriptions s WHERE s.subscription_group_id=g.subscription_group_id AND last_error IS NOT NULL), origin, "
				"(SELECT COALESCE(sum(f.size),0)::bigint FROM file_info f WHERE EXISTS ("
				"SELECT 1 FROM subscriptions s JOIN subscription_records log USING (subscription_id) "
				"WHERE s.subscription_group_id=g.subscription_group_id AND log.record_id=f.record_id)) "
				"FROM subscription_groups g WHERE ($1::integer=0 OR subscription_group_id=$1) ORDER BY subscription_group_id",
				std::move( bindings ) )
			.rows<
				SubscriptionGroupID,
				std::string,
				std::string,
				std::int64_t,
				bool,
				std::int64_t,
				std::int64_t,
				std::int64_t,
				std::int64_t,
				std::int64_t,
				std::string,
				std::int64_t >()
	};
	std::vector< SubscriptionGroup > result;
	for (
		const auto& [ group_id, name, generator, interval, enabled, items, records, running, dead, errors, origin, total_size_bytes ] :
		rows )
		result.push_back(
			{ group_id, name, generator, interval, enabled, items, records, running, dead, errors, origin, total_size_bytes } );

	co_return result;
}

runtime::Task< std::vector< SubscriptionGroup > > subscriptionGroups( DBContext& db )
{
	co_return co_await loadSubscriptionGroups( db, 0 );
}

runtime::Task< std::optional< SubscriptionGroup > > findSubscriptionGroup( DBContext& db, const SubscriptionGroupID id )
{
	auto groups { co_await loadSubscriptionGroups( db, id ) };
	if ( groups.empty() ) co_return std::nullopt;

	co_return std::move( groups.front() );
}

runtime::Task< SubscriptionGroupID > createSubscriptionGroup( DBContext& db, SubscriptionGroup group )
{
	DBBindings bindings;
	bindings.bind( group.name );
	bindings.bind( group.generator );
	bindings.bind( group.interval_seconds );
	bindings.bind( group.enabled );
	bindings.bind( group.origin );
	const auto rows {
		co_await db
			.query(
				"INSERT INTO subscription_groups (name, generator, interval_seconds, enabled, origin) VALUES ($1,$2,$3,$4,$5) "
				"RETURNING subscription_group_id",
				std::move( bindings ) )
			.rows< SubscriptionGroupID >()
	};
	co_return std::get< 0 >( rows.row( 0 ) );
}

runtime::Task< bool > updateSubscriptionGroup( DBContext& db, SubscriptionGroup group )
{
	auto transaction { co_await db.transaction() };
	DBBindings lookup;
	lookup.bind( group.id );
	const auto current {
		co_await transaction
			.query(
				"SELECT generator, interval_seconds, origin FROM subscription_groups WHERE subscription_group_id=$1 FOR UPDATE",
				std::move( lookup ) )
			.rows< std::string, std::int64_t, std::string >()
	};
	if ( current.empty() ) co_return false;

	const auto& [ generator, interval, origin ] { current.row( 0 ) };
	const bool changed_generator { generator != group.generator || origin != group.origin };
	if ( changed_generator )
	{
		DBBindings active_bindings;
		active_bindings.bind( group.id );
		const auto active {
			co_await transaction
				.query(
					"SELECT subscription_id FROM subscriptions WHERE subscription_group_id=$1 AND active_root_id IS NOT NULL LIMIT 1",
					std::move( active_bindings ) )
				.rows< SubscriptionID >()
		};
		if ( !active.empty() ) co_return false;
	}

	DBBindings bindings;
	bindings.bind( group.id );
	bindings.bind( group.name );
	bindings.bind( group.generator );
	bindings.bind( group.interval_seconds );
	bindings.bind( group.enabled );
	bindings.bind( group.origin );
	co_await transaction
		.query(
			"UPDATE subscription_groups SET name=$2, generator=$3, interval_seconds=$4, enabled=$5, origin=$6 "
			"WHERE subscription_group_id=$1",
			std::move( bindings ) )
		.result();
	if ( changed_generator )
	{
		DBBindings reset;
		reset.bind( group.id );
		co_await transaction
			.query(
				"UPDATE subscriptions SET initial_sync_complete=false, latest_post_at=NULL, post_dates_complete=false, dead=false, "
				"next_run_at=now(), last_error=NULL WHERE subscription_group_id=$1",
				std::move( reset ) )
			.result();
	}
	else if ( interval != group.interval_seconds )
	{
		DBBindings reschedule;
		reschedule.bind( group.id );
		reschedule.bind( group.interval_seconds );
		co_await transaction
			.query(
				"UPDATE subscriptions SET next_run_at=COALESCE(last_finished_at + $2::bigint * interval '1 second', now()) "
				"WHERE subscription_group_id=$1 AND active_session_id IS NULL",
				std::move( reschedule ) )
			.result();
	}

	co_await transaction.commit();
	co_return true;
}

runtime::Task< bool > deleteSubscriptionGroup( DBContext& db, const SubscriptionGroupID id )
{
	auto transaction { co_await db.transaction() };
	DBBindings lookup;
	lookup.bind( id );
	const auto group {
		co_await transaction
			.query(
				"SELECT subscription_group_id FROM subscription_groups WHERE subscription_group_id=$1 FOR UPDATE",
				std::move( lookup ) )
			.rows< SubscriptionGroupID >()
	};
	if ( group.empty() ) co_return false;

	DBBindings bindings;
	bindings.bind( id );
	const auto deleted {
		co_await transaction
			.query(
				"DELETE FROM subscription_groups WHERE subscription_group_id=$1 AND NOT EXISTS ("
				"SELECT 1 FROM subscriptions WHERE subscription_group_id=$1 AND active_session_id IS NOT NULL) RETURNING subscription_group_id",
				std::move( bindings ) )
			.rows< SubscriptionGroupID >()
	};
	co_await transaction.commit();
	co_return !deleted.empty();
}

runtime::Task< bool > requestSubscriptionGroupRun( DBContext& db, const SubscriptionGroupID id )
{
	DBBindings bindings;
	bindings.bind( id );
	const auto groups {
		co_await db
			.query(
				"WITH selected AS (SELECT subscription_group_id FROM subscription_groups WHERE subscription_group_id=$1 AND enabled), "
				"scheduled AS (UPDATE subscriptions SET next_run_at=now() WHERE enabled AND active_session_id IS NULL "
				"AND subscription_group_id IN (SELECT subscription_group_id FROM selected)) SELECT subscription_group_id FROM selected",
				std::move( bindings ) )
			.rows< SubscriptionGroupID >()
	};
	co_return !groups.empty();
}
} // namespace idhan::db
