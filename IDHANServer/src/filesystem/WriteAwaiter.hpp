//
// Created by kj16609 on 8/1/25.
//
#pragma once

#include <coroutine>
#include <exception>
#include <liburing.h>

#include "fgl/defines.hpp"

namespace trantor
{
class EventLoop;
}

namespace idhan
{
class IOUring;

struct [[nodiscard]] WriteAwaiter
{
	struct promise_type;
	using handle_type = std::coroutine_handle< promise_type >;

	bool await_ready() const noexcept { return false; }

	void await_suspend( const std::coroutine_handle<> h );

	void await_resume();

	std::exception_ptr m_exception { nullptr };
	std::coroutine_handle<> m_cont {};

	IOUring* m_uring { nullptr };
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
	io_uring_sqe m_sqe {};
#pragma GCC diagnostic pop
	trantor::EventLoop* m_event_loop { nullptr };

	WriteAwaiter( IOUring* uring, io_uring_sqe sqe );

	FGL_DELETE_ALL_RO5( WriteAwaiter );

	void complete( int result );

	~WriteAwaiter();
};

} // namespace idhan
