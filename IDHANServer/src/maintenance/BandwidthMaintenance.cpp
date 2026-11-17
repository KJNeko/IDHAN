#include "BandwidthMaintenance.hpp"

#include <folly/coro/Task.h>

#include <chrono>
#include <condition_variable>
#include <mutex>

#include "db/bandwidth.hpp"
#include "logging/log.hpp"

namespace idhan::maintenance
{
BandwidthMaintenance::BandwidthMaintenance( db::DBContext& db, folly::Executor::KeepAlive<> executor ) :
  m_db( db ),
  m_executor( std::move( executor ) ),
  m_thread( [ this ]( const std::stop_token stop ) { run( stop ); } )
{}

BandwidthMaintenance::~BandwidthMaintenance()
{
	m_thread.request_stop();
	if ( m_thread.joinable() ) m_thread.join();
}

void BandwidthMaintenance::run( const std::stop_token stop )
{
	std::mutex mutex;
	std::condition_variable_any wake;
	std::vector< downloader::BandwidthLog > pending;
	for ( ;; )
	{
		if ( pending.empty() ) pending = downloader::takeBandwidthLogs();

		if ( !pending.empty() )
		{
			try
			{
				m_job.activity( "working", "Persisting queued bandwidth measurements" );
				folly::coro::co_withExecutor( m_executor, db::storeBandwidth( m_db, pending ) ).start().get();
				m_job.processed( pending.size() );
				pending.clear();
				// Drain full batches without waiting another interval, including on shutdown.
				continue;
			}
			catch ( const std::exception& error )
			{
				m_job.recordFailure( error.what() );
				log::warn( "Bandwidth flush failed: {}", error.what() );
				if ( stop.stop_requested() )
				{
					m_job.finish( std::string { error.what() } );
					return;
				}
			}
		}

		if ( stop.stop_requested() ) break;

		m_job.activity( "waiting", "Next bandwidth flush in 5 seconds" );
		std::unique_lock lock { mutex };
		wake.wait_for( lock, stop, std::chrono::seconds { 5 }, [] { return false; } );
	}
	m_job.finish();
}
} // namespace idhan::maintenance
