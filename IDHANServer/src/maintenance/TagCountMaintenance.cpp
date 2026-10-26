#include "TagCountMaintenance.hpp"

#include <folly/coro/Task.h>

#include <chrono>
#include <condition_variable>
#include <format>
#include <mutex>
#include <utility>

#include "db/tagCounts.hpp"
#include "logging/log.hpp"

namespace idhan::maintenance
{
TagCountMaintenance::TagCountMaintenance( db::DBContext& db, folly::Executor::KeepAlive<> executor ) :
  m_db( db ),
  m_executor( std::move( executor ) ),
  m_thread( [ this ]( const std::stop_token stop ) { run( stop ); } )
{}

TagCountMaintenance::~TagCountMaintenance()
{
	m_job.activity( "stopping", "Waiting for tag count accumulation to finish" );
	m_thread.request_stop();
	if ( m_thread.joinable() ) m_thread.join();
}

void TagCountMaintenance::run( const std::stop_token stop )
{
	std::mutex mutex;
	std::condition_variable_any wake;
	while ( !stop.stop_requested() )
	{
		try
		{
			m_job.activity( "working", "Accumulating committed tag count deltas" );
			const auto batch {
				folly::coro::co_withExecutor( m_executor, db::accumulateTagCountDeltas( m_db ) ).start().get()
			};
			m_job.processed( batch.deltas );
			m_job.activity(
				"waiting",
				std::format(
					"Consumed {} deltas, updated {} counters; next pass in 60 seconds",
					batch.deltas,
					batch.counters ) );
		}
		catch ( const std::exception& error )
		{
			m_job.recordFailure( error.what() );
			m_job.activity( "retrying", "Tag count accumulation failed; retrying in 60 seconds" );
			log::warn( "Tag count accumulation failed: {}", error.what() );
		}
		std::unique_lock lock { mutex };
		wake.wait_for( lock, stop, std::chrono::seconds { 60 }, [] { return false; } );
	}
	m_job.finish();
}
} // namespace idhan::maintenance
