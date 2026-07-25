//
// Created by kj16609 on 2/27/26.
//
#pragma once
#include <coroutine>
#include <memory>

#include "drogon/HttpResponse.h"

#ifdef TRACY_ENABLE
	#include "idhan_tracy/CoroFiber.hpp"

	#include <string>
#endif

struct JobTaskStatus;
struct JobTask;

struct JobTaskPromise
{
	std::shared_ptr< JobTaskStatus > m_status;

	// Add constructor
	JobTaskPromise();

	// Implement get_return_object
	JobTask get_return_object();

#ifdef TRACY_ENABLE
	idhan::tracy_coro::FiberCtx m_fiber_ctx { idhan::tracy_coro::makeChildCtx( "job" ) };
	const char* m_fiber_name { idhan::tracy_coro::internFiberNameFor( m_fiber_ctx ) };

	idhan::tracy_coro::FiberInitialAwaiter initial_suspend();

	idhan::tracy_coro::FiberFinalAwaiter< std::suspend_always > final_suspend() noexcept;

	template < typename Awaitable >
	auto await_transform( Awaitable&& awaitable )
	{
		return idhan::tracy_coro::FiberAwaiter< Awaitable > { std::forward< Awaitable >( awaitable ), m_fiber_name, m_fiber_ctx
		};
	}
#else
	std::suspend_always initial_suspend();

	std::suspend_always final_suspend() noexcept;
#endif

	void return_void() {}

	void unhandled_exception();
};
