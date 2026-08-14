#include <gtest/gtest.h>

#include <coroutine>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

#include "coro/Task.hpp"

idhan::coro::Task< int > answer()
{
	co_return 42;
}

idhan::coro::Task< int > doubled()
{
	const auto value { co_await answer() };
	co_return value * 2;
}

idhan::coro::Task< std::string > moveOnlyPayload()
{
	std::string big( 1024, 'x' );
	co_return big;
}

idhan::coro::Task< int > thrower()
{
	throw std::runtime_error( "boom" );
	co_return 0;
}

idhan::coro::Task< int > catcher()
{
	try
	{
		co_return co_await thrower();
	}
	catch ( const std::runtime_error& )
	{
		co_return -1;
	}
}

int g_side_effect { 0 };

idhan::coro::Task<> bumpSideEffect()
{
	g_side_effect = 1;
	co_return;
}

idhan::coro::Task<> awaitVoid()
{
	co_await bumpSideEffect();
	g_side_effect += 1;
	co_return;
}

struct SyncDriver
{
	struct promise_type
	{
		SyncDriver get_return_object() const noexcept { return {}; }

		static std::suspend_never initial_suspend() noexcept { return {}; }

		static std::suspend_never final_suspend() noexcept { return {}; }

		void return_void() const noexcept {}

		void unhandled_exception() const { std::terminate(); }
	};
};

template < typename T >
SyncDriver driveInto( idhan::coro::Task< T > task, T* out )
{
	*out = co_await task;
}

SyncDriver driveVoid( idhan::coro::Task<> task )
{
	co_await task;
}

template < typename T >
T syncRun( idhan::coro::Task< T > task )
{
	T out {};
	driveInto< T >( std::move( task ), &out );
	return out;
}


TEST( CoroTask, returnsValue )
{
	EXPECT_EQ( syncRun( answer() ), 42 );
}

TEST( CoroTask, awaitsNestedTask )
{
	EXPECT_EQ( syncRun( doubled() ), 84 );
}

TEST( CoroTask, movesPayloadOut )
{
	EXPECT_EQ( syncRun( moveOnlyPayload() ).size(), 1024u );
}

TEST( CoroTask, propagatesExceptionThroughAwait )
{
	EXPECT_EQ( syncRun( catcher() ), -1 );
}

TEST( CoroTask, isLazy )
{
	g_side_effect = 0;
	{
		// Constructed but never awaited: initial_suspend is suspend_always, so the body must not run.
		auto task { bumpSideEffect() };
		EXPECT_EQ( g_side_effect, 0 );
	}
	EXPECT_EQ( g_side_effect, 0 );
}

TEST( CoroTask, voidSpecialisationRuns )
{
	g_side_effect = 0;

	driveVoid( awaitVoid() );

	EXPECT_EQ( g_side_effect, 2 );
}
