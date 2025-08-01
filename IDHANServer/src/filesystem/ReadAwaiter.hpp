//
// Created by kj16609 on 8/1/25.
//
#pragma once
#include <coroutine>
#include <exception>
#include <liburing.h>
#include <vector>

namespace trantor
{
class EventLoop;
}

namespace idhan
{

class IOUring;

struct [[nodiscard]] ReadAwaiter
{
	struct promise_type;

	using handle_type = std::coroutine_handle< promise_type >;

	bool await_ready() const noexcept { return false; }

	void await_suspend( const std::coroutine_handle<> h );

	std::vector< std::byte > await_resume();

	handle_type m_h;

	std::vector< std::byte > m_data {};

	std::exception_ptr m_exception {};
	std::coroutine_handle<> m_cont;

	IOUring* m_uring { nullptr };
	io_uring_sqe m_sqe {};
	trantor::EventLoop* m_event_loop { nullptr };

	ReadAwaiter( handle_type handle ) : m_h( handle ) {}

	ReadAwaiter( IOUring* uring, io_uring_sqe sqe );

	void complete( int result, const std::vector< std::byte >& data );

	~ReadAwaiter();
};

} // namespace idhan