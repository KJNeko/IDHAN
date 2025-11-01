//
// Created by kj16609 on 8/1/25.
//

#include "ReadAwaiter.hpp"

#include <liburing.h>

#include "IOUring.hpp"
#include "logging/log.hpp"
#include "spdlog/fmt/bundled/format.h"
#include "trantor/net/EventLoop.h"

namespace idhan
{

ReadAwaiter::ReadAwaiter( IOUring* uring, io_uring_sqe sqe, std::shared_ptr< std::vector< std::byte > >& data ) :
  m_data( data ),
  m_cont(),
  m_uring( uring ),
  m_sqe( sqe )
{}

void ReadAwaiter::complete( const int result )
{
	if ( result < 0 )
	{
		log::error( "Failed to read file: {}", strerror( errno ) );
		m_exception =
			std::make_exception_ptr( std::runtime_error( std::string( "Failed to read file: " ) + strerror( errno ) ) );
	}

	// delete reinterpret_cast< IOUringUserData* >( m_sqe.user_data );

	if ( !m_cont )
	{
		log::critical( "ReadWaiter had no coroutine to continue!" );
	}

	if ( m_cont.done() )
	{
		log::critical( "ReadWaiter had a coroutine that was already finished!" );
	}

	// m_cont.resume();
	m_event_loop->queueInLoop( m_cont );
}

bool ReadAwaiter::await_ready() const noexcept
{
	return !m_uring;
}

void ReadAwaiter::await_suspend( const std::coroutine_handle<> h )
{
	m_event_loop = trantor::EventLoop::getEventLoopOfCurrentThread();

	if ( !h || h.done() )
	{
		log::critical( "Coroutine handle was null or finished" );
	}

	const auto ptr = h.address();

	// Check if the memory at ptr is a 0 byte pointer
	if ( *reinterpret_cast< std::size_t** >( ptr ) == nullptr )
	{
		log::critical( "Internal pointer address was zeroed" );
	}

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

std::vector< std::byte > ReadAwaiter::await_resume() const
{
	if ( m_exception ) std::rethrow_exception( m_exception );
	return *m_data;
}

ReadAwaiter::~ReadAwaiter()
{}

} // namespace idhan
