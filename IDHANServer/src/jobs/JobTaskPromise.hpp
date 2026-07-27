//
// Created by kj16609 on 2/27/26.
//
#pragma once
#include <coroutine>
#include <memory>

#include "drogon/HttpResponse.h"

struct JobTaskStatus;
struct JobTask;
struct JobTaskPromise;

//! Final awaiter for a job coroutine.
//!
//! Completion is published from await_suspend rather than from final_suspend() itself. JobRuntime's
//! cleanup thread destroys a job's handle as soon as it observes m_done, so the flag must not become
//! visible while the frame is still running: final_suspend() returns to compiler-generated code that
//! is still executing in the frame, whereas by the time await_suspend is called the coroutine is
//! fully suspended and another thread may legally destroy it. Nothing here touches the frame after
//! the store.
struct JobFinalAwaiter
{
	[[nodiscard]] static bool await_ready() noexcept { return false; }

	void await_suspend( std::coroutine_handle< JobTaskPromise > handle ) const noexcept;

	void await_resume() const noexcept {}
};

struct JobTaskPromise
{
	std::shared_ptr< JobTaskStatus > m_status;

	// Add constructor
	JobTaskPromise();

	// Implement get_return_object
	JobTask get_return_object();

	std::suspend_always initial_suspend();

	JobFinalAwaiter final_suspend() noexcept;

	void return_void() {}

	void unhandled_exception();
};
