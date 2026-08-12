#include "coro/RunLoop.hpp"

namespace idhan::coro
{

void RunLoop::post( const std::coroutine_handle<> handle )
{
	{
		std::lock_guard lock { m_mtx };
		m_queue.push_back( handle );
	}

	m_cv.notify_one();
}

void RunLoop::run()
{
	while ( true )
	{
		std::coroutine_handle<> handle {};

		{
			std::unique_lock lock { m_mtx };
			m_cv.wait( lock, [ this ] { return m_stopping || !m_queue.empty(); } );

			// Woken with nothing left to do means stop() has been called and the queue has drained.
			// The queue is checked before the flag so a stop never discards already-queued work.
			if ( m_queue.empty() ) return;

			handle = m_queue.front();
			m_queue.pop_front();
		}

		// Resumed outside the lock: the coroutine body is free to post() back onto this loop.
		handle.resume();
	}
}

void RunLoop::stop()
{
	{
		std::lock_guard lock { m_mtx };
		m_stopping = true;
	}

	m_cv.notify_all();
}

void RunLoopResumer::resume( const std::coroutine_handle<> handle ) noexcept
{
	m_loop->post( handle );
}

} // namespace idhan::coro
