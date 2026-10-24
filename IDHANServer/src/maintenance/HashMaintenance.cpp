#include "HashMaintenance.hpp"

#include <folly/coro/Task.h>

#include <chrono>
#include <condition_variable>
#include <format>
#include <mutex>

#include "hamming/hamming.hpp"
#include "logging/log.hpp"
#include "metadata/phash.hpp"

namespace idhan::maintenance
{
static void waitForNextSweep( const std::stop_token stop, const std::chrono::seconds delay )
{
	std::mutex mutex;
	std::condition_variable_any wake;
	std::unique_lock lock { mutex };
	wake.wait_for( lock, stop, delay, [] { return false; } );
}

HashMaintenance::HashMaintenance( db::DBContext& db, folly::Executor::KeepAlive<> executor ) :
  m_db( db ),
  m_executor( std::move( executor ) ),
  m_hamming( [ this ]( const std::stop_token stop ) { runHamming( stop ); } ),
  m_phash( [ this ]( const std::stop_token stop ) { runPhash( stop ); } )
{}

HashMaintenance::~HashMaintenance()
{
	stop();
}

void HashMaintenance::stop()
{
	m_hamming_job.activity( "stopping", "Waiting for the current Hamming batch to finish" );
	m_phash_job.activity( "stopping", "Waiting for the current perceptual hash operation to finish" );
	m_hamming.request_stop();
	m_phash.request_stop();
	if ( m_hamming.joinable() ) m_hamming.join();
	if ( m_phash.joinable() ) m_phash.join();
}

void HashMaintenance::runHamming( const std::stop_token stop )
{
	const std::chrono::seconds active_delay { 1 };
	const std::chrono::seconds idle_delay { std::chrono::minutes { 5 } };
	auto delay { active_delay };
	while ( !stop.stop_requested() )
	{
		try
		{
			m_hamming_job.activity( "working", "Checking the Hamming distance queue" );
			const auto processed {
				folly::coro::co_withExecutor( m_executor, hamming::runQueueSweep( m_db, stop ) ).start().get()
			};
			m_hamming_job.processed( processed );
			delay = processed == 0 ? idle_delay : active_delay;
			m_hamming_job.activity(
				"waiting",
				std::format(
					"{}; next Hamming distance sweep in {} seconds",
					processed == 0 ? "Queue is empty" : "Sweep complete",
					delay.count() ) );
		}
		catch ( const std::exception& error )
		{
			delay = active_delay;
			m_hamming_job.recordFailure( error.what() );
			m_hamming_job.activity( "retrying", "Sweep failed; retrying in one second" );
			log::warn( "Hamming distance sweep failed: {}", error.what() );
		}
		waitForNextSweep( stop, delay );
	}
	m_hamming_job.finish();
}

void HashMaintenance::runPhash( const std::stop_token stop )
{
	try
	{
		m_phash_job.activity( "working", "Checking for missing perceptual hashes" );
		folly::coro::co_withExecutor( m_executor, metadata::runPhashSweep( m_db, stop, &m_phash_job ) ).start().get();
	}
	catch ( const std::exception& error )
	{
		m_phash_job.recordFailure( error.what() );
		log::warn( "Missing phash sweep failed: {}", error.what() );
	}
	m_phash_job.finish();
}
} // namespace idhan::maintenance
