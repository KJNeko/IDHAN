//
// Created by kj16609 on 8/1/25.
//
#pragma once

#include <coroutine>
#include <exception>
#include <liburing.h>

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

	handle_type m_h;

	std::exception_ptr m_exception;
	std::coroutine_handle<> m_cont;

	IOUring* m_uring { nullptr };
	io_uring_sqe m_sqe {};
	trantor::EventLoop* m_event_loop { nullptr };

	WriteAwaiter( handle_type handle ) : m_h( handle ) {}

	WriteAwaiter( IOUring* uring, io_uring_sqe sqe );

	void complete( int result );

	~WriteAwaiter();
};

} // namespace idhan