//
// Created by kj16609 on 8/1/25.
//
#include "WriteAwaiter.hpp"

#include <liburing.h>

#include "IOUring.hpp"
#include "logging/log.hpp"
#include "trantor/net/EventLoop.h"

namespace idhan
{

void WriteAwaiter::await_suspend( const std::coroutine_handle<> h )
{
	m_event_loop = trantor::EventLoop::getEventLoopOfCurrentThread();
	m_cont = h;

	std::lock_guard lock { m_uring->mtx };

	unsigned tail = *m_uring->m_submission_ring.tail;
	unsigned index = tail & *m_uring->m_submission_ring.mask;

	m_sqe.user_data = reinterpret_cast< decltype( m_sqe.user_data ) >( new IOUringUserData( this ) );
	m_uring->m_submission_ring.entries[ index ] = m_sqe;
	m_uring->m_submission_ring.array[ index ] = index;
	tail++;

	io_uring_smp_store_release( m_uring->m_submission_ring.tail, tail );

	m_uring->notifySubmit( 1 );
}

void WriteAwaiter::await_resume()
{
	if ( m_exception ) std::rethrow_exception( m_exception );
	return;
}

WriteAwaiter::WriteAwaiter( IOUring* uring, io_uring_sqe sqe ) : m_uring( uring ), m_sqe( sqe )
{}

void WriteAwaiter::complete( int result )
{
	if ( result < 0 )
	{
		log::error( "Failed to read file: {}", strerror( errno ) );
		m_exception =
			std::make_exception_ptr( std::runtime_error( std::string( "Failed to read file: " ) + strerror( errno ) ) );
	}

	if ( m_cont )
	{
		m_event_loop->queueInLoop( m_cont );
		// m_cont.resume();
	}
}

WriteAwaiter::~WriteAwaiter()
{}

} // namespace idhan