//
// Created by kj16609 on 8/1/25.
//
#pragma once
#ifdef __linux__

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

class IOUringLinux;

struct [[nodiscard]] WriteAwaiter
{
	static bool await_ready() noexcept;
	void await_suspend( std::coroutine_handle<> h );
	void await_resume() const;

	std::exception_ptr m_exception { nullptr };
	std::coroutine_handle<> m_cont {};
	IOUringLinux* m_uring { nullptr };
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
	io_uring_sqe m_sqe {};
#pragma GCC diagnostic pop
	trantor::EventLoop* m_event_loop { nullptr };

	WriteAwaiter( IOUringLinux* uring, io_uring_sqe sqe );

	FGL_DELETE_ALL_RO5( WriteAwaiter );

	void complete( int result );

	~WriteAwaiter();
};

} // namespace idhan

#endif // __linux__
