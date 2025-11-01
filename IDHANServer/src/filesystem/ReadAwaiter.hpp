//
// Created by kj16609 on 8/1/25.
//
#pragma once
#include <coroutine>
#include <exception>
#include <liburing.h>
#include <memory>
#include <vector>

#include "fgl/defines.hpp"

namespace trantor
{
class EventLoop;
}

namespace idhan
{

class IOUring;

struct [[nodiscard]] ReadAwaiter
{
	std::shared_ptr< std::vector< std::byte > > m_data {};

	std::exception_ptr m_exception {};
	std::coroutine_handle<> m_cont;

	IOUring* m_uring { nullptr };
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
	io_uring_sqe m_sqe {};
#pragma GCC diagnostic pop
	trantor::EventLoop* m_event_loop { nullptr };

	FGL_DELETE_COPY( ReadAwaiter );

	FGL_DELETE_MOVE( ReadAwaiter );

	ReadAwaiter( IOUring* uring, io_uring_sqe sqe, std::shared_ptr< std::vector< std::byte > >& data );

	void complete( int result );

	[[nodiscard]] bool await_ready() const noexcept;

	void await_suspend( const std::coroutine_handle<> h );

	std::vector< std::byte > await_resume() const;

	~ReadAwaiter();
};

} // namespace idhan
