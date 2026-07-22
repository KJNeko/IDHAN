//
// Created by kj16609 on 2/27/26.
//

#include "JobTaskPromise.hpp"

#include <drogon/HttpResponse.h>

#include <chrono>
#include <exception>

#include "JobTask.hpp"
#include "JobTaskStatus.hpp"

JobTaskPromise::JobTaskPromise() : m_status( std::make_shared< JobTaskStatus >() )
{}

JobTask JobTaskPromise::get_return_object()
{
	return JobTask { std::coroutine_handle< JobTaskPromise >::from_promise( *this ) };
}

#ifdef TRACY_ENABLE
idhan::tracy_coro::FiberInitialAwaiter JobTaskPromise::initial_suspend()
{
	return { m_fiber_name.c_str() };
}
#else
std::suspend_always JobTaskPromise::initial_suspend()
{
	return {};
}
#endif

#ifdef TRACY_ENABLE
idhan::tracy_coro::FiberFinalAwaiter< std::suspend_always > JobTaskPromise::final_suspend() noexcept
{
	m_status->m_completion_time = std::chrono::steady_clock::now();
	m_status->m_done = true;
	return { std::suspend_always {} };
}
#else
std::suspend_always JobTaskPromise::final_suspend() noexcept
{
	m_status->m_completion_time = std::chrono::steady_clock::now();
	m_status->m_done = true;
	return {};
}
#endif

void JobTaskPromise::unhandled_exception()
{
	m_status->m_failed = true;
	try
	{
		std::rethrow_exception( std::current_exception() );
	}
	catch ( const std::exception& e )
	{
		m_status->m_error_message = e.what();
	}
	catch ( ... )
	{
		m_status->m_error_message = "Unknown exception";
	}
}
