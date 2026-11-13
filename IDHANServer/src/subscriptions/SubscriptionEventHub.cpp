#include "SubscriptionEventHub.hpp"

#include "events/ServerEvents.hpp"
#include "logging/log.hpp"

namespace idhan::subscriptions
{
SubscriptionEventHub& SubscriptionEventHub::instance()
{
	static SubscriptionEventHub hub;
	return hub;
}

void SubscriptionEventHub::sessionRecordsChanged( const DownloadSessionID session_id )
{
	if ( !server::events().interested( "subscriptions.changed" ) ) return;

	m_pending.enqueue( session_id );
}

runtime::Task<> SubscriptionEventHub::refreshSessions( db::DBContext& db, std::vector< DownloadSessionID > session_ids )
{
	if ( !server::events().interested( "subscriptions.changed" ) ) co_return;

	const auto lock { co_await m_mutex.co_scoped_lock() };
	const auto groups { co_await db::subscriptionGroupsForSessions( db, std::move( session_ids ) ) };
	for ( const auto group_id : groups ) co_await refreshLocked( db, group_id );
}

void SubscriptionEventHub::runUpdates(
	db::DBContext& db,
	folly::Executor::KeepAlive<> executor,
	const std::stop_token stop )
{
	while ( !stop.stop_requested() )
	{
		auto sessions { m_pending.waitForBatch( stop ) };
		if ( stop.stop_requested() ) return;

		try
		{
			folly::coro::co_withExecutor( executor, refreshSessions( db, std::move( sessions ) ) ).start().get();
		}
		catch ( const std::exception& error )
		{
			// The scheduler's reconciliation repairs counts if a database refresh failed.
			log::warn( "Subscription count update failed: {}", error.what() );
		}
	}
}

runtime::Task<> SubscriptionEventHub::refreshLocked(
	db::DBContext& db,
	const std::optional< SubscriptionGroupID > group_id )
{
	std::vector< db::SubscriptionGroup > groups;
	std::vector< db::Subscription > items;
	if ( group_id.has_value() )
	{
		auto group { co_await db::findSubscriptionGroup( db, group_id.value() ) };
		if ( group.has_value() ) groups.push_back( std::move( group.value() ) );

		items = co_await db::subscriptionsInGroup( db, group_id.value() );
	}
	else
	{
		groups = co_await db::subscriptionGroups( db );
		items = co_await db::subscriptions( db );
	}
	for ( auto& event : m_state.reconcile( std::move( groups ), std::move( items ), group_id ) )
		server::events().publish(
			"subscriptions.changed",
			"subscriptions",
			std::move( event ),
			{ .retain_snapshot = false, .coalesce = false } );
}

runtime::Task<> SubscriptionEventHub::refresh( db::DBContext& db, const std::optional< SubscriptionGroupID > group_id )
{
	if ( !server::events().interested( "subscriptions.changed" ) ) co_return;

	const auto lock { co_await m_mutex.co_scoped_lock() };
	co_await refreshLocked( db, group_id );
}

runtime::Task<> SubscriptionEventHub::initializeSubscription( db::DBContext& db, http::SSESubscription& subscription )
{
	const auto lock { co_await m_mutex.co_scoped_lock() };
	co_await refreshLocked( db, std::nullopt );
	std::vector< http::SSEInitialEvent > initial;
	for ( auto& snapshot : m_state.snapshots() )
		initial.push_back( { "subscriptions.snapshot", "subscriptions", std::move( snapshot ) } );
	subscription.prependSnapshots( std::move( initial ), subscription.checkpoint(), "subscriptions" );
}
} // namespace idhan::subscriptions
