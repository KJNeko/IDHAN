#pragma once

#include <condition_variable>
#include <coroutine>
#include <deque>
#include <exception>
#include <mutex>
#include <optional>
#include <utility>

#include "coro/Resumer.hpp"
#include "coro/Task.hpp"
#include "fgl/defines.hpp"

namespace idhan::coro
{

//! A queue of coroutine handles pumped by one thread. post() is callable from any thread; run()
//! owns the thread it is called on until stop() is called and the queue has drained.
class RunLoop
{
	std::mutex m_mtx {};
	std::condition_variable m_cv {};
	std::deque< std::coroutine_handle<> > m_queue {};
	bool m_stopping { false };

  public:

	RunLoop() = default;
	~RunLoop() = default;

	FGL_DELETE_COPY( RunLoop );
	FGL_DELETE_MOVE( RunLoop );

	//! Thread-safe. Queues `handle` to be resumed on the thread inside run().
	void post( std::coroutine_handle<> handle );

	//! Pumps queued handles on the calling thread. Returns once stop() has been called AND the queue
	//! is empty, so work queued before or during the stop is never dropped.
	void run();

	//! Thread-safe. Asks run() to return once the queue has drained. Calling this before run() is
	//! entered is legal and makes run() a drain-and-return.
	void stop();
};

//! Resumer that hands continuations to a RunLoop. Safe to call from the io watcher thread.
class RunLoopResumer final : public Resumer
{
	RunLoop* m_loop;

  public:

	explicit RunLoopResumer( RunLoop& loop ) noexcept : m_loop( &loop ) {}

	FGL_DELETE_COPY( RunLoopResumer );
	FGL_DELETE_MOVE( RunLoopResumer );

	void resume( std::coroutine_handle<> handle ) noexcept override;
};

namespace detail
{

//! Eagerly-started, self-destroying coroutine used only to drive a Task from non-coroutine code.
struct DetachedTask
{
	struct promise_type
	{
		DetachedTask get_return_object() const noexcept { return {}; }

		static std::suspend_never initial_suspend() noexcept { return {}; }

		static std::suspend_never final_suspend() noexcept { return {}; }

		void return_void() const noexcept {}

		void unhandled_exception() const { std::terminate(); }
	};
};

template < typename T >
DetachedTask driveTask( Task< T > task, std::optional< T >* out, std::exception_ptr* error, RunLoop* loop )
{
	try
	{
		out->emplace( co_await task );
	}
	catch ( ... )
	{
		*error = std::current_exception();
	}

	loop->stop();
}

inline DetachedTask driveTask( Task< void > task, std::exception_ptr* error, RunLoop* loop )
{
	try
	{
		co_await task;
	}
	catch ( ... )
	{
		*error = std::current_exception();
	}

	loop->stop();
}

} // namespace detail

//! Runs `task` to completion, pumping `loop` on the calling thread until it finishes, and returns
//! its value. Rethrows whatever the task threw.
template < typename T >
T runOnLoop( RunLoop& loop, Task< T > task )
{
	std::optional< T > out {};
	std::exception_ptr error {};

	detail::driveTask< T >( std::move( task ), &out, &error, &loop );
	loop.run();

	if ( error ) std::rethrow_exception( error );

	return std::move( *out );
}

//! void overload of runOnLoop.
inline void runOnLoop( RunLoop& loop, Task< void > task )
{
	std::exception_ptr error {};

	detail::driveTask( std::move( task ), &error, &loop );
	loop.run();

	if ( error ) std::rethrow_exception( error );
}

} // namespace idhan::coro
