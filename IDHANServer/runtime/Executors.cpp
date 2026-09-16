#include "idhan/server/Executors.hpp"

#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/thread_factory/NamedThreadFactory.h>

#include <mutex>
#include <stdexcept>
#include <utility>

namespace idhan::server
{
[[nodiscard]] static std::shared_ptr< folly::CPUThreadPoolExecutor > makeExecutor(
	std::size_t threads,
	const char* name )
{
	if ( threads == 0 ) throw std::invalid_argument( "Each server executor requires at least one thread" );

	return std::make_shared< folly::CPUThreadPoolExecutor >(
		threads, std::make_shared< folly::NamedThreadFactory >( name ) );
}

Executors::Executors(
	const std::size_t io_threads,
	const std::size_t application_threads,
	const std::size_t heavy_threads,
	const std::size_t database_threads ) :
  m_application( makeExecutor( application_threads, "idhan-app-" ) ),
  m_heavy( makeExecutor( heavy_threads, "idhan-heavy-" ) ),
  m_database( makeExecutor( database_threads, "idhan-db-" ) ),
  m_io_threads( io_threads )
{
	if ( io_threads == 0 ) throw std::invalid_argument( "The server requires at least one I/O event loop" );
}

Executors::~Executors()
{
	stop();
}

folly::Executor::KeepAlive<> Executors::application() const
{
	return folly::getKeepAliveToken( m_application.get() );
}

folly::Executor::KeepAlive<> Executors::heavy() const
{
	return folly::getKeepAliveToken( m_heavy.get() );
}

folly::Executor::KeepAlive<> Executors::database() const
{
	return folly::getKeepAliveToken( m_database.get() );
}

std::size_t Executors::ioThreadCount() const noexcept
{
	return m_io_threads;
}

runtime::Task<> Executors::start()
{
	if ( m_state.load( std::memory_order_acquire ) != ExecutorState::running )
		throw std::logic_error( "Cannot start stopped server executors" );

	co_return;
}

runtime::Task<> Executors::drain()
{
	if ( m_state.load( std::memory_order_acquire ) == ExecutorState::stopped ) co_return;

	std::call_once(
		m_drain_once,
		[ this ]
		{
			m_application->join();
			m_heavy->join();
			m_database->join();
			m_state.store( ExecutorState::drained, std::memory_order_release );
		} );
}

void Executors::stop() noexcept
{
	std::call_once(
		m_stop_once,
		[ this ]
		{
			try
			{
				m_application->stop();
				m_heavy->stop();
				m_database->stop();
			}
			catch ( ... )
			{
				std::terminate();
			}
			m_state.store( ExecutorState::stopped, std::memory_order_release );
		} );
}
} // namespace idhan::server
