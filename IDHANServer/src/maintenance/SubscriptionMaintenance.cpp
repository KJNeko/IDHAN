#include "SubscriptionMaintenance.hpp"

#include <chrono>
#include <condition_variable>
#include <format>
#include <mutex>

#include "db/subscriptions.hpp"
#include "downloader/DownloadSessionManager.hpp"
#include "downloader/GeneratorSession.hpp"
#include "downloader/SessionResult.hpp"
#include "idhan/db/DBContext.hpp"
#include "idhan/db/DBTransaction.hpp"
#include "logging/log.hpp"
#include "subscriptions/SubscriptionEventHub.hpp"

namespace idhan::maintenance
{
static std::string subscriptionLabel( const db::Subscription& subscription )
{
	if ( !subscription.tags.empty() ) return subscription.tags;

	std::string label;
	for ( const auto& [ key, value ] : subscription.parameters )
	{
		if ( !label.empty() ) label += " · ";

		label += value;
	}
	return label;
}

runtime::Task<> SubscriptionMaintenance::pollSubscriptions()
{
	auto& downloads { downloader::downloadSessionManager() };

	const auto items { co_await db::subscriptions( m_db ) };
	for ( const auto& item : items )
	{
		if ( !item.active_session_id.has_value() ) continue;

		const auto session_id { item.active_session_id.value() };
		auto& job { m_runs[ item.id ] };
		if ( !job )
			job = std::make_unique< jobs::SubscriptionJob >(
				db::SubscriptionRunResult { item.id, session_id }, subscriptionLabel( item ) );

		if ( !downloads.idle( session_id ) ) continue;

		const auto outcome { co_await downloader::sessionResult( m_db, session_id ) };
		if ( !outcome.has_value() )
		{
			// A committed activation may outlive a failed submission or completion callback.
			const auto retried_e { co_await downloader::retrySession( m_db, session_id ) };
			if ( !retried_e.has_value() )
				log::warn( "Subscription session {} failed: {}", session_id, retried_e.error() );

			continue;
		}

		const bool finished { co_await db::finishSubscription( m_db, item.id, session_id, outcome->error ) };
		if ( !finished ) continue;

		job->finish( outcome->error );
		m_runs.erase( item.id );
		co_await downloads.destroy( session_id );
	}

	const auto groups { co_await db::subscriptionGroups( m_db ) };
	for ( const auto& group : groups )
	{
		if ( !group.enabled ) continue;

		if ( group.running_count != 0 ) continue;

		auto transaction { co_await m_db.transaction() };
		const auto next { co_await db::nextScheduledSubscription( transaction, group.id ) };
		if ( !next.has_value() ) continue;

		const auto& check { next.value() };
		const downloader::GeneratorRequest request { check.generator, check.origin, check.parameters };
		DownloadSessionID session_id {};
		if ( check.session_id.has_value() )
		{
			session_id = check.session_id.value();
			co_await downloader::configureGeneratorSession( transaction, session_id, request );
		}
		else
		{
			session_id = co_await downloader::createGeneratorSession( transaction, check.id, request );
		}
		co_await db::startSubscription( transaction, check.id, session_id );
		co_await transaction.commit();
		const auto retried_e { co_await downloader::retrySession( m_db, session_id ) };
		if ( !retried_e.has_value() ) log::warn( "Subscription session {} failed: {}", session_id, retried_e.error() );
	}
	co_await subscriptions::SubscriptionEventHub::instance().refresh( m_db );
}

SubscriptionMaintenance::SubscriptionMaintenance( db::DBContext& db, folly::Executor::KeepAlive<> executor ) :
  m_db( db ),
  m_executor( std::move( executor ) ),
  m_thread( [ this ]( std::stop_token stop ) { run( stop ); } )
{}

SubscriptionMaintenance::~SubscriptionMaintenance()
{
	m_thread.request_stop();
	if ( m_thread.joinable() ) m_thread.join();
}

void SubscriptionMaintenance::run( const std::stop_token stop )
{
	while ( !stop.stop_requested() )
	{
		const auto next_tick { std::chrono::steady_clock::now() + std::chrono::seconds { 30 } };
		try
		{
			m_job.activity( "working", "Checking scheduled subscriptions" );
			folly::coro::co_withExecutor( m_executor, pollSubscriptions() ).start().get();
		}
		catch ( const std::exception& error )
		{
			m_job.recordFailure( error.what() );
			log::warn( "Subscription scheduler failed: {}", error.what() );
		}

		m_job.activity( "waiting", "Next subscription check in 30 seconds" );
		std::unique_lock lock { m_wait_mutex };
		m_wait.wait_until( lock, stop, next_tick, [] { return false; } );
	}
	m_job.finish();
}
} // namespace idhan::maintenance
