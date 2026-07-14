//
// Created by kj16609 on 2/23/26.
//
#pragma once
#include <coroutine>
#include <memory>

#include "JobTaskPromise.hpp"
#include "fgl/defines.hpp"

//! Coroutine return type for a job body run by JobRuntime. Owns the coroutine handle and its shared
//! JobTaskStatus (job id, stored result response, completion state). See queueJob().
struct JobTask
{
	using promise_type = JobTaskPromise;
	using Handle = std::coroutine_handle< promise_type >;

	std::shared_ptr< JobTaskStatus > m_status;

	// Add handle member
	Handle m_handle {};

	// Add constructors, destructor, and move operations
	JobTask() = delete; // For completed tasks

	JobTask( Handle h );

	FGL_DELETE_COPY( JobTask );

	JobTask( JobTask&& other ) noexcept;

	JobTask& operator=( JobTask&& other ) noexcept;

	~JobTask();
};