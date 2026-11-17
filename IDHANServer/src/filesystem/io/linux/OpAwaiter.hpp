#pragma once
#ifdef __linux__

#include <coroutine>
#include <liburing.h>

#include "fgl/defines.hpp"

namespace folly
{
class Executor;
}

namespace idhan
{

class IOUringLinux;

//! Returns raw path-operation results without throwing or logging; callers choose error handling
//! and fallbacks for unsupported opcodes.
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
	folly::Executor* m_executor { nullptr };
	int m_result { 0 };

	OpAwaiter( IOUringLinux* uring, io_uring_sqe sqe, folly::Executor* executor );

	FGL_DELETE_COPY( OpAwaiter );
	// Folly moves this into ViaIfAsyncAwaitable before await_suspend captures `this`.
	FGL_DEFAULT_MOVE( OpAwaiter );

	void complete( int result );

	~OpAwaiter();
};

} // namespace idhan

#endif
