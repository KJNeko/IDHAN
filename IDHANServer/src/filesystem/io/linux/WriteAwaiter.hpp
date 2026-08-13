#pragma once
#ifdef __linux__

#include <coroutine>
#include <cstddef>
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

	//! Bytes actually accepted by this one op. Throws if the op failed. A write op has pwrite(2)
	//! semantics and may accept fewer bytes than it was given, so the caller must loop on this
	//! rather than assume the whole buffer landed.
	[[nodiscard]] std::size_t await_resume() const;

	std::exception_ptr m_exception { nullptr };
	int m_result { 0 };
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
