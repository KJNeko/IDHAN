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

std::suspend_always JobTaskPromise::initial_suspend()
{
	return {};
}

void JobFinalAwaiter::await_suspend( const std::coroutine_handle< JobTaskPromise > handle ) const noexcept
{
	const std::shared_ptr< JobTaskStatus > status { handle.promise().m_status };

	status->m_completion_time = std::chrono::steady_clock::now();

	// Release: pairs with the cleanup thread's load of m_done, so it sees m_completion_time written.
	status->m_done.store( true, std::memory_order_release );
}

JobFinalAwaiter JobTaskPromise::final_suspend() noexcept
{
	return {};
}

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
