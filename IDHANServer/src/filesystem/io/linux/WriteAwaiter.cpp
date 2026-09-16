#ifdef __linux__

#include "filesystem/io/linux/WriteAwaiter.hpp"

#include <liburing.h>

#include <folly/Executor.h>

#include "filesystem/io/linux/IOUringLinux.hpp"
#include "logging/log.hpp"

namespace idhan
{

bool WriteAwaiter::await_ready() noexcept
{
	return false;
}

void WriteAwaiter::await_suspend( const std::coroutine_handle<> h )
{
	m_cont = h;

	std::lock_guard lock { m_uring->mtx };

	unsigned tail { *m_uring->m_submission_ring.tail };
	const unsigned index { tail & *m_uring->m_submission_ring.mask };

	m_sqe.user_data = reinterpret_cast< decltype( m_sqe.user_data ) >( new IOUringUserData( this ) );
	m_uring->m_submission_ring.entries[ index ] = m_sqe;
	m_uring->m_submission_ring.array[ index ] = index;
	tail++;

	io_uring_smp_store_release( m_uring->m_submission_ring.tail, tail );
	m_uring->notifySubmit( 1 );
}

std::size_t WriteAwaiter::await_resume() const
{
	if ( m_exception ) std::rethrow_exception( m_exception );

	return static_cast< std::size_t >( m_result );
}

WriteAwaiter::WriteAwaiter( IOUringLinux* uring, io_uring_sqe sqe, folly::Executor* executor ) :
  m_uring( uring ),
  m_sqe( sqe ),
  m_executor( executor )
{}

void WriteAwaiter::complete( const int result )
{
	m_result = result;

	if ( result < 0 )
	{
		// result is -errno from the io_uring completion, not the thread-local errno
		const auto message { format_ns::format(
			"Failed to write file: {} (fd {}, offset {}, {} bytes)",
			strerror( -result ),
			m_sqe.fd,
			m_sqe.off,
			m_sqe.len ) };

		log::error( message );
		m_exception = std::make_exception_ptr( std::runtime_error( message ) );
	}

	if ( m_cont ) m_executor->add( [ cont = m_cont ] { cont.resume(); } );
}

WriteAwaiter::~WriteAwaiter() = default;

} // namespace idhan

#endif // __linux__
