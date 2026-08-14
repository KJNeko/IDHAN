#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

#include "coro/RunLoop.hpp"
#include "coro/Task.hpp"

idhan::coro::RunLoop* g_loop { nullptr };

//! Suspends, hands its continuation to another thread, and is resumed through the RunLoop -- the
//! same shape the io awaiters have, without needing io_uring.
struct OffThreadAwaiter
{
	std::thread m_worker {};

	[[nodiscard]] bool await_ready() const noexcept { return false; }

	void await_suspend( const std::coroutine_handle<> handle )
	{
		m_worker = std::thread(
			[ handle ]()
			{
				std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
				g_loop->post( handle );
			} );
	}

	void await_resume()
	{
		if ( m_worker.joinable() ) m_worker.join();
	}
};

idhan::coro::Task< int > resumedElsewhere()
{
	co_await OffThreadAwaiter {};
	co_return 7;
}

idhan::coro::Task< int > immediate()
{
	co_return 11;
}

idhan::coro::Task< int > failing()
{
	throw std::runtime_error( "run loop task failed" );
	co_return 0;
}

idhan::coro::Task<> voidTask( std::atomic< int >* counter )
{
	counter->fetch_add( 1 );
	co_return;
}


TEST( CoroRunLoop, resumesATaskCompletedOnAnotherThread )
{
	idhan::coro::RunLoop loop {};
	g_loop = &loop;

	EXPECT_EQ( idhan::coro::runOnLoop( loop, resumedElsewhere() ), 7 );

	g_loop = nullptr;
}

TEST( CoroRunLoop, handlesATaskThatNeverSuspends )
{
	// stop() lands before run() is entered. run() must drain and return rather than block forever.
	idhan::coro::RunLoop loop {};
	EXPECT_EQ( idhan::coro::runOnLoop( loop, immediate() ), 11 );
}

TEST( CoroRunLoop, rethrowsTaskException )
{
	idhan::coro::RunLoop loop {};
	EXPECT_THROW( ( void ) idhan::coro::runOnLoop( loop, failing() ), std::runtime_error );
}

TEST( CoroRunLoop, runsVoidTasks )
{
	idhan::coro::RunLoop loop {};
	std::atomic< int > counter { 0 };

	idhan::coro::runOnLoop( loop, voidTask( &counter ) );

	EXPECT_EQ( counter.load(), 1 );
}

TEST( CoroRunLoop, resumerPostsToTheLoop )
{
	idhan::coro::RunLoop loop {};
	idhan::coro::RunLoopResumer resumer { loop };

	resumer.resume( std::noop_coroutine() );
	loop.stop();
	loop.run();

	SUCCEED();
}
