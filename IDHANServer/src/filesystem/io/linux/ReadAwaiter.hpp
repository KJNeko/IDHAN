#pragma once
#ifdef __linux__

#include <coroutine>
#include <exception>
#include <liburing.h>
#include <memory>
#include <vector>

#include "fgl/defines.hpp"

namespace folly
{
class Executor;
}

namespace idhan
{

class IOUringLinux;

struct [[nodiscard]] ReadAwaiter
{
	std::shared_ptr< std::vector< std::byte > > m_data {};
	int m_result { -1 };
	std::exception_ptr m_exception {};
	std::coroutine_handle<> m_cont;
	IOUringLinux* m_uring { nullptr };
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
	io_uring_sqe m_sqe {};
#pragma GCC diagnostic pop
	folly::Executor* m_executor { nullptr };

	FGL_DELETE_COPY( ReadAwaiter );
	// Moved into folly's ViaIfAsyncAwaitable before await_suspend ever captures `this`, so this is safe.
	FGL_DEFAULT_MOVE( ReadAwaiter );

	ReadAwaiter(
		IOUringLinux* uring,
		io_uring_sqe sqe,
		std::shared_ptr< std::vector< std::byte > >& data,
		folly::Executor* executor );

	void complete( int result );

	[[nodiscard]] bool await_ready() const noexcept;
	void await_suspend( std::coroutine_handle<> h );
	[[nodiscard]] std::vector< std::byte > await_resume();

	~ReadAwaiter();
};

} // namespace idhan

#endif // __linux__
