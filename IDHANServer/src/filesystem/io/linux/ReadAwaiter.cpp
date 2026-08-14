#ifdef __linux__

#include "filesystem/io/linux/ReadAwaiter.hpp"

#include <liburing.h>

#include "filesystem/io/linux/IOUringLinux.hpp"
#include "logging/log.hpp"
#include "trantor/net/EventLoop.h"

namespace idhan
{

ReadAwaiter::ReadAwaiter( IOUringLinux* uring, io_uring_sqe sqe, std::shared_ptr< std::vector< std::byte > >& data ) :
  m_data( data ),
  m_cont(),
  m_uring( uring ),
  m_sqe( sqe )
{}

void ReadAwaiter::complete( const int result )
{
	m_result = result;

	if ( result < 0 )
	{
		// result is -errno from the io_uring completion, not the thread-local errno
		log::error( "Failed to read file: {}", strerror( -result ) );
		m_exception = std::make_exception_ptr(
			std::runtime_error( std::string( "Failed to read file: " ) + strerror( -result ) ) );
	}

	if ( !m_cont ) log::critical( "ReadAwaiter had no coroutine to resume" );
	if ( m_cont.done() ) log::critical( "ReadAwaiter coroutine was already finished" );

	m_event_loop->queueInLoop( m_cont );
}

bool ReadAwaiter::await_ready() const noexcept
{
	return !m_uring;
}

void ReadAwaiter::await_suspend( const std::coroutine_handle<> h )
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

std::vector< std::byte > ReadAwaiter::await_resume()
{
	if ( m_exception ) std::rethrow_exception( m_exception );

	if ( m_result >= 0 ) m_data->resize( static_cast< std::size_t >( m_result ) );

	return std::move( *m_data );
}

ReadAwaiter::~ReadAwaiter() = default;

} // namespace idhan

#endif // __linux__
