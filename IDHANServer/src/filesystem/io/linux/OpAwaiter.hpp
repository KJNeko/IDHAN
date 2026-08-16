#pragma once
#ifdef __linux__

#include <coroutine>
#include <liburing.h>

#include "fgl/defines.hpp"

namespace trantor
{
class EventLoop;
}

namespace idhan
{

class IOUringLinux;

//! Awaits any completion whose result is just an integer: unlinkat, renameat, mkdirat, statx.
//!
//! Unlike ReadAwaiter/WriteAwaiter this never throws and never logs. Those two carry an error
//! message specific to reading or writing file contents; a path op has no single right reaction to
//! a negative result. Removing a file that is already gone is usually fine, an unsupported opcode
//! means fall back to the blocking call, and a failed rename is fatal. So the raw result is handed
//! back and the caller decides.
struct [[nodiscard]] OpAwaiter
{
	static bool await_ready() noexcept;
	void await_suspend( std::coroutine_handle<> h );

	//! 0 or a positive count on success, a negative errno on failure.
	[[nodiscard]] int await_resume() const;

	std::coroutine_handle<> m_cont {};
	IOUringLinux* m_uring { nullptr };
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
	io_uring_sqe m_sqe {};
#pragma GCC diagnostic pop
	trantor::EventLoop* m_event_loop { nullptr };
	int m_result { 0 };

	OpAwaiter( IOUringLinux* uring, io_uring_sqe sqe );

	FGL_DELETE_ALL_RO5( OpAwaiter );

	void complete( int result );

	~OpAwaiter();
};

} // namespace idhan

#endif // __linux__
