#include "HashMaintenance.hpp"

#include <folly/coro/Task.h>

#include <chrono>
#include <condition_variable>
#include <mutex>

#include "hamming/hamming.hpp"
#include "logging/log.hpp"
#include "metadata/phash.hpp"

namespace idhan::maintenance
{
static void waitForNextSweep( const std::stop_token stop )
{
	std::mutex mutex;
	std::condition_variable_any wake;
	std::unique_lock lock { mutex };
	wake.wait_for( lock, stop, std::chrono::seconds { 1 }, [] { return false; } );
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
	m_hamming.request_stop();
	m_phash.request_stop();
	if ( m_hamming.joinable() ) m_hamming.join();
	if ( m_phash.joinable() ) m_phash.join();
}

void HashMaintenance::runHamming( const std::stop_token stop )
{
	while ( !stop.stop_requested() )
	{
		try
		{
			folly::coro::co_withExecutor( m_executor, hamming::runQueueSweep( m_db, stop ) ).start().get();
		}
		catch ( const std::exception& error )
		{
			log::warn( "Hamming distance sweep failed: {}", error.what() );
		}
		waitForNextSweep( stop );
	}
}

void HashMaintenance::runPhash( const std::stop_token stop )
{
	while ( !stop.stop_requested() )
	{
		try
		{
			folly::coro::co_withExecutor( m_executor, metadata::runPhashSweep( m_db, stop ) ).start().get();
		}
		catch ( const std::exception& error )
		{
			log::warn( "Missing phash sweep failed: {}", error.what() );
		}
		waitForNextSweep( stop );
	}
}
} // namespace idhan::maintenance
