#pragma once
#include <coroutine>
#include <memory>

#include "JobTaskPromise.hpp"
#include "fgl/defines.hpp"

struct JobTask
{
	using promise_type = JobTaskPromise;
	using Handle = std::coroutine_handle< promise_type >;

	std::shared_ptr< JobTaskStatus > m_status;

	Handle m_handle {};

	JobTask() = delete;

	JobTask( Handle h );

	FGL_DELETE_COPY( JobTask );

	JobTask( JobTask&& other ) noexcept;

	JobTask& operator=( JobTask&& other ) noexcept;

	~JobTask();
};
