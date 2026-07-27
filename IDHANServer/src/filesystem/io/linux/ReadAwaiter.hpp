//
// Created by kj16609 on 8/1/25.
//
#pragma once
#ifdef __linux__

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

class IOUringLinux;

struct [[nodiscard]] ReadAwaiter
{
	std::shared_ptr< std::vector< std::byte > > m_data {};
	// Byte count from the io_uring completion, recorded by complete(). -1 means the awaiter never
	// suspended (await_ready() true), in which case the buffer is left exactly as the caller sized it.
	int m_result { -1 };
	std::exception_ptr m_exception {};
	std::coroutine_handle<> m_cont;
	IOUringLinux* m_uring { nullptr };
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
	io_uring_sqe m_sqe {};
#pragma GCC diagnostic pop
	trantor::EventLoop* m_event_loop { nullptr };

	FGL_DELETE_COPY( ReadAwaiter );
	FGL_DELETE_MOVE( ReadAwaiter );

	ReadAwaiter( IOUringLinux* uring, io_uring_sqe sqe, std::shared_ptr< std::vector< std::byte > >& data );

	void complete( int result );

	[[nodiscard]] bool await_ready() const noexcept;
	void await_suspend( std::coroutine_handle<> h );
	[[nodiscard]] std::vector< std::byte > await_resume();

	~ReadAwaiter();
};

} // namespace idhan

#endif // __linux__
