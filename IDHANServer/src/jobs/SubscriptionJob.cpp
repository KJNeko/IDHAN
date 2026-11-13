#include "SubscriptionJob.hpp"

#include <folly/coro/Baton.h>

#include <format>
#include <mutex>
#include <stdexcept>

#include "JobContext.hpp"

namespace idhan::jobs
{
class SubscriptionJob::Progress final : public JobProgress
{
  public:

	using JobProgress::changed;
	mutable std::mutex mutex {};
	folly::coro::Baton completion {};
	db::SubscriptionRunResult result {};
	bool finished {};
	std::optional< std::string > error {};

	Json::Value snapshot() const override
	{
		std::lock_guard lock { mutex };
		Json::Value value { Json::objectValue };
		value[ "kind" ] = "subscription";
		value[ "subscription_id" ] = result.subscription_id;
		if ( result.session_id ) value[ "session_id" ] = result.session_id.value();
		if ( result.root_id ) value[ "root_id" ] = result.root_id.value();
		value[ "imported_count" ] = result.imported_count;
		value[ "known_count" ] = result.known_count;
		if ( error ) value[ "error" ] = error.value();
		return value;
	}
};

JobTask SubscriptionJob::waitForRun( std::shared_ptr< Progress > progress )
{
	co_await setJobProgress( progress );
	co_await progress->completion;
	co_await setJobResponse( progress->snapshot() );
	if ( progress->error ) throw std::runtime_error( progress->error.value() );
}

SubscriptionJob::SubscriptionJob( db::SubscriptionRunResult result, std::string label ) :
  m_progress( std::make_shared< Progress >() )
{
	m_progress->result = result;
	(void)queueJob( waitForRun( m_progress ), std::format( "Subscription {}: {}", result.subscription_id, label ) );
}

SubscriptionJob::~SubscriptionJob()
{
	finish( "Run tracking stopped; download recovery will resume after restart" );
}

void SubscriptionJob::update( db::SubscriptionRunResult result )
{
	std::lock_guard lock { m_progress->mutex };
	if ( m_progress->finished ) return;

	m_progress->result = result;
	m_progress->changed();
}

void SubscriptionJob::finish( std::optional< std::string > error )
{
	{
		std::lock_guard lock { m_progress->mutex };
		if ( m_progress->finished ) return;

		m_progress->finished = true;
		m_progress->error = std::move( error );
	}
	m_progress->completion.post();
}
} // namespace idhan::jobs
