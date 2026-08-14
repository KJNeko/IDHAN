#pragma once
#include <coroutine>
#include <memory>

#include "drogon/HttpResponse.h"

struct JobTaskStatus;
struct JobTask;
struct JobTaskPromise;

//! Publishes completion only once the coroutine is fully suspended; cleanup may destroy the frame as
//! soon as it observes m_done.
struct JobFinalAwaiter
{
	[[nodiscard]] static bool await_ready() noexcept { return false; }

	void await_suspend( std::coroutine_handle< JobTaskPromise > handle ) const noexcept;

	void await_resume() const noexcept {}
};

struct JobTaskPromise
{
	std::shared_ptr< JobTaskStatus > m_status;

	JobTaskPromise();

	JobTask get_return_object();

	std::suspend_always initial_suspend();

	JobFinalAwaiter final_suspend() noexcept;

	void return_void() {}

	void unhandled_exception();
};
