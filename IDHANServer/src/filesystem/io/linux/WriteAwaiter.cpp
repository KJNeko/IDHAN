#ifdef __linux__

#include "filesystem/io/linux/WriteAwaiter.hpp"

#include <liburing.h>

#include "filesystem/io/linux/IOUringLinux.hpp"
#include "logging/log.hpp"
#include "trantor/net/EventLoop.h"

namespace idhan
{

bool WriteAwaiter::await_ready() noexcept
{
	return false;
}

void WriteAwaiter::await_suspend( const std::coroutine_handle<> h )
{
	m_event_loop = trantor::EventLoop::getEventLoopOfCurrentThread();
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

WriteAwaiter::WriteAwaiter( IOUringLinux* uring, io_uring_sqe sqe ) : m_uring( uring ), m_sqe( sqe )
{}

void WriteAwaiter::complete( const int result )
{
	// Only the integer is recorded here -- this runs on the io watcher thread. Whether the count is
	// short is decided by the caller, on the thread the coroutine resumes on.
	m_result = result;

	if ( result < 0 )
	{
		// result is -errno from the io_uring completion, not the thread-local errno
		log::error( "Failed to write file: {}", strerror( -result ) );
		m_exception = std::make_exception_ptr(
			std::runtime_error( std::string( "Failed to write file: " ) + strerror( -result ) ) );
	}

	if ( m_cont ) m_event_loop->queueInLoop( m_cont );
}

WriteAwaiter::~WriteAwaiter() = default;

} // namespace idhan

#endif // __linux__
