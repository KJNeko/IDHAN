#ifdef __linux__

#include "filesystem/io/linux/OpAwaiter.hpp"

#include <liburing.h>

#include "filesystem/io/linux/IOUringLinux.hpp"
#include "logging/log.hpp"
#include "trantor/net/EventLoop.h"

namespace idhan
{

bool OpAwaiter::await_ready() noexcept
{
	return false;
}

void OpAwaiter::await_suspend( const std::coroutine_handle<> h )
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

int OpAwaiter::await_resume() const
{
	return m_result;
}

OpAwaiter::OpAwaiter( IOUringLinux* uring, io_uring_sqe sqe ) : m_uring( uring ), m_sqe( sqe )
{}

void OpAwaiter::complete( const int result )
{
	m_result = result;

	if ( !m_cont )
	{
		log::critical( "OpAwaiter had no coroutine to resume" );
		return;
	}

	m_event_loop->queueInLoop( m_cont );
}

OpAwaiter::~OpAwaiter() = default;

} // namespace idhan

#endif // __linux__
