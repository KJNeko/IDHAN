#include "BackgroundJob.hpp"

#include <folly/coro/Baton.h>

#include <chrono>
#include <mutex>
#include <stdexcept>
#include <utility>

#include "JobContext.hpp"

namespace idhan::jobs
{
class BackgroundJob::Progress final : public JobProgress
{
  public:

	using JobProgress::changed;

	explicit Progress( std::string service_name ) : service( std::move( service_name ) ) {}

	mutable std::mutex mutex {};
	folly::coro::Baton completion {};
	const std::string service;
	std::string phase { "starting" };
	std::string message { "Starting service" };
	std::uint64_t processed_units {};
	std::uint64_t failed_count {};
	std::string last_error {};
	std::optional< std::string > terminal_error {};
	bool finished {};
	std::chrono::system_clock::time_point updated_at { std::chrono::system_clock::now() };

	[[nodiscard]] Json::Value snapshot() const override
	{
		std::lock_guard lock { mutex };
		Json::Value result { Json::objectValue };
		result[ "kind" ] = "background_service";
		result[ "service" ] = service;
		result[ "phase" ] = phase;
		result[ "message" ] = message;
		result[ "processed_units" ] = static_cast< Json::UInt64 >( processed_units );
		result[ "failed_count" ] = static_cast< Json::UInt64 >( failed_count );
		result[ "updated_at" ] =
			std::chrono::duration_cast< std::chrono::seconds >( updated_at.time_since_epoch() ).count();
		if ( !last_error.empty() ) result[ "last_error" ] = last_error;
		return result;
	}
};

JobTask BackgroundJob::waitForStop( std::shared_ptr< Progress > progress )
{
	co_await setJobProgress( progress );
	co_await progress->completion;
	// finish() publishes terminal_error before posting completion. It is immutable afterwards.
	if ( progress->terminal_error ) throw std::runtime_error( *progress->terminal_error );
}

BackgroundJob::BackgroundJob(
	const std::string_view name,
	std::string service,
	const bool continuous,
	const std::source_location location ) :
  m_progress( std::make_shared< Progress >( std::move( service ) ) ),
  m_job( queueJob( waitForStop( m_progress ), name, location, continuous ) )
{}

BackgroundJob::~BackgroundJob()
{
	finish();
}

void BackgroundJob::activity( std::string phase, std::string message )
{
	std::lock_guard lock { m_progress->mutex };
	if ( m_progress->finished ) return;

	m_progress->phase = std::move( phase );
	m_progress->message = std::move( message );
	m_progress->updated_at = std::chrono::system_clock::now();
	m_progress->changed();
}

void BackgroundJob::processed( const std::uint64_t count )
{
	std::lock_guard lock { m_progress->mutex };
	if ( m_progress->finished ) return;

	m_progress->processed_units += count;
	m_progress->updated_at = std::chrono::system_clock::now();
	m_progress->changed();
}

void BackgroundJob::recordFailure( std::string message )
{
	std::lock_guard lock { m_progress->mutex };
	if ( m_progress->finished ) return;

	++m_progress->failed_count;
	m_progress->last_error = std::move( message );
	m_progress->updated_at = std::chrono::system_clock::now();
	m_progress->changed();
}

void BackgroundJob::finish( std::optional< std::string > error )
{
	{
		std::lock_guard lock { m_progress->mutex };
		if ( m_progress->finished ) return;

		m_progress->finished = true;
		m_progress->terminal_error = std::move( error );
		m_progress->phase = m_progress->terminal_error ? "failed" : "stopped";
		m_progress->message = m_progress->terminal_error.value_or( "Service stopped" );
		m_progress->updated_at = std::chrono::system_clock::now();
		m_progress->changed();
	}
	// Posting can resume and finish the job inline. Never hold the snapshot mutex here.
	m_progress->completion.post();
}
} // namespace idhan::jobs
