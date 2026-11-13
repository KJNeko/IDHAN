#include "SubscriptionMaintenance.hpp"

#include <chrono>
#include <condition_variable>
#include <format>
#include <mutex>

#include "db/subscriptions.hpp"
#include "downloader/DownloadSessionManager.hpp"
#include "downloader/SessionUrlIds.hpp"
#include "downloader/downloaderEvents.hpp"
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
	auto& db { m_db };
	auto& downloads { downloader::downloadSessionManager() };
	const auto subscriptions { co_await db::subscriptions( db ) };
	for ( const auto& subscription : subscriptions )
	{
		if ( !subscription.active_session_id.has_value() ) continue;

		const auto session_id { subscription.active_session_id.value() };
		const auto result { co_await db::subscriptionRunResult( db, subscription.id ) };
		auto& job { m_runs[ subscription.id ] };
		if ( !job ) job = std::make_unique< jobs::SubscriptionJob >( result, subscriptionLabel( subscription ) );

		job->update( result );
		if ( !downloads.idle( session_id ) ) continue;

		co_await db::finishSubscription( db, subscription.id, session_id );
		const auto finished { co_await db::findSubscription( db, subscription.id ) };
		if ( finished && !finished->active_session_id )
		{
			job->finish( finished->last_error );
			m_runs.erase( subscription.id );
		}
		co_await downloads.destroy( session_id );
	}

	co_await db::refreshSubscriptionActivity( db );
	const auto claimed { co_await db::claimSubscription( db ) };
	co_await idhan::subscriptions::SubscriptionEventHub::instance().refresh( db );
	if ( !claimed.has_value() ) co_return;

	const auto& run { claimed.value() };
	for ( const auto& subscription : subscriptions )
	{
		if ( subscription.id != run.id ) continue;

		m_runs[ run.id ] = std::make_unique< jobs::SubscriptionJob >(
			db::SubscriptionRunResult {
				.subscription_id = run.id, .session_id = run.session_id, .root_id = run.root_id },
			subscriptionLabel( subscription ) );
		break;
	}
	downloader::SessionUrlIds::instance().create( run.session_id );
	downloader::SessionUrlIds::instance().ensureNext( run.session_id, run.root_id + 1 );
	co_await downloads.destroy( run.session_id );
	const auto now {
		std::chrono::duration_cast< std::chrono::seconds >( std::chrono::system_clock::now().time_since_epoch() )
			.count()
	};
	downloader::events::sessionCreated(
		{ .id = run.session_id,
		  .name = std::format( "Subscription {} / {}", run.id, run.session_id ),
		  .created_at = now,
		  .last_used_at = now,
		  .is_subscription = true } );
	if ( !run.recovering )
		downloader::events::urlCreated(
			run.session_id,
			{ .url_id = run.root_id,
			  .url = run.url,
			  .created_at = now,
			  .state = downloader::events::UrlState { .state = "pending" } } );
	std::optional< std::string > error;
	try
	{
		const auto submitted_e { co_await downloads.submit(
			run.root_id, run.session_id, run.url, run.settings, std::nullopt, run.recovering ) };
		if ( !submitted_e.has_value() ) error = submitted_e.error();
	}
	catch ( const std::exception& exception )
	{
		error = exception.what();
	}

	if ( error.has_value() )
		co_await downloads.markUnresumable( { run.root_id, run.session_id, run.url, run.settings }, error.value(), db );
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

void SubscriptionMaintenance::wake()
{
	{
		const std::scoped_lock lock { s_wake_mutex };
		s_woken = true;
	}
	s_wake.notify_all();
}

void SubscriptionMaintenance::run( const std::stop_token stop )
{
	while ( !stop.stop_requested() )
	{
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

		m_job.activity( "waiting", "Next subscription check in 5 seconds" );
		std::unique_lock lock { s_wake_mutex };
		s_wake.wait_for( lock, stop, std::chrono::seconds { 5 }, [] { return s_woken; } );
		s_woken = false;
	}
	m_job.finish();
}
} // namespace idhan::maintenance
