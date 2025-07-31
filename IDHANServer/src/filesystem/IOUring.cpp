//
// Created by kj16609 on 7/29/25.
//
#include "IOUring.hpp"

#include <sys/mman.h>

#include <cmath>
#include <liburing.h>
#include <stdexcept>

#include "drogon/HttpAppFramework.h"
#include "logging/format_ns.hpp"
#include "logging/log.hpp"

namespace idhan
{

void ReadAwaiter::await_suspend( const std::coroutine_handle<> h )
{
	m_cont = h;

	static std::mutex mtx {};
	std::lock_guard lock { mtx };

	unsigned tail = *m_uring->m_submission_ring.tail;
	unsigned index = tail & *m_uring->m_submission_ring.mask;

	m_sqe.user_data = reinterpret_cast< unsigned long >( this );
	m_uring->m_submission_ring.entries[ index ] = m_sqe;

	m_uring->m_submission_ring.array[ index ] = index;
	tail++;

	io_uring_smp_store_release( m_uring->m_submission_ring.tail, tail );

	m_uring->notifySubmit( 1 );
}

std::vector< std::byte > ReadAwaiter::await_resume()
{
	if ( m_exception ) std::rethrow_exception( m_exception );
	return m_data;
}

ReadAwaiter::ReadAwaiter( IOUring* uring, struct io_uring_sqe sqe ) : m_uring( uring ), m_sqe( sqe )
{}

void ReadAwaiter::complete( int result, const std::vector< std::byte >& data )
{
	if ( result < 0 )
	{
		log::error( "Failed to read file: {}", strerror( errno ) );
		m_exception =
			std::make_exception_ptr( std::runtime_error( std::string( "Failed to read file: " ) + strerror( errno ) ) );
	}
	else
	{
		m_data = data;
	}

	if ( m_cont ) m_cont.resume();
}

ReadAwaiter::~ReadAwaiter()
{}

drogon::Task< std::vector< std::byte > > FileIOUring::read( const std::size_t offset, const std::size_t len )
{
	auto& uring { IOUring::getInstance() };

	if ( m_fd <= 0 ) throw std::runtime_error( "FileIOUring::read: Invalid file descriptor" );

	if ( len >= std::numeric_limits< __u32 >::max() )
		throw std::runtime_error(
			format_ns::format(
				"FileIOUring::read: len > std::numeric_limits<unsigned>::max() ({} >= {}) Tell the Dev!",
				len,
				std::numeric_limits< __u32 >::max() ) );

	io_uring_sqe sqe {};
	std::memset( &sqe, 0, sizeof( sqe ) );

	std::vector< std::byte > data {};
	data.resize( len );
	std::memset( data.data(), 0, len );

	io_uring_prep_read( &sqe, m_fd, data.data(), static_cast< __u32 >( len ), offset );

	co_await uring.send( sqe );

	co_return data;
}

struct UserData
{
	ReadAwaiter* awaiter;
};

void ioThread( const std::stop_token& token, IOUring* uring, std::shared_ptr< std::atomic< bool > > running )
{
	const auto min_complete { 1 };

	if ( running->load() == false ) running->wait( false );

	while ( !token.stop_requested() )
	{
		// We can check the CQ ring tail for things being inserted.
		FGL_ASSERT( uring->uring_fd > 0, "Invalid io_uring fd" );
		const auto ret { io_uring_enter( uring->uring_fd, 0, 1, IORING_ENTER_GETEVENTS, nullptr ) };

		if ( ret < 0 )
		{
			log::error( "Failed to enter io_uring, Error code: {}", ret );
			std::this_thread::sleep_for( std::chrono::seconds( 5 ) );
			continue;
		}

		unsigned head { io_uring_smp_load_acquire( uring->m_command_ring.head ) };

		log::debug( "Processing events from io_uring" );
		while ( head != io_uring_smp_load_acquire( uring->m_command_ring.tail ) )
		{
			log::debug( "Processing CQ entry" );
			const unsigned index = head & *uring->m_command_ring.mask;
			const auto& cqe = uring->m_command_ring.cqes[ index ];

			if ( cqe.res < 0 )
			{
				log::error( "Error: {}", strerror( abs( cqe.res ) ) );
				head++;
				continue;
			}

			if ( cqe.user_data == 0 )
			{
				log::error( "Awaiter was not given" );
				head++;
				continue;
			}

			auto* awaiter { reinterpret_cast< ReadAwaiter* >( cqe.user_data ) };

			if ( awaiter )
			{
				std::vector< std::byte > data {};

				auto* buffer { reinterpret_cast< std::byte* >( awaiter->m_sqe.addr ) };
				data.assign( buffer, buffer + cqe.res );

				awaiter->complete( cqe.res, data );
			}
			else
			{
				log::warn( "Something happened to the awaiter for io_uring" );
			}

			head++;
		}

		io_uring_smp_store_release( uring->m_command_ring.head, head );

		std::this_thread::sleep_for( std::chrono::milliseconds( 25 ) );
	}
}

IOUring::SubmissionRingPointers IOUring::setupSubmissionRing()
{
	SubmissionRingPointers ptrs {};

	if ( m_params.features & IORING_FEAT_SINGLE_MMAP )
	{
		const auto sq_len { m_params.sq_off.array + m_params.sq_entries * sizeof( unsigned ) };
		const auto cq_len { m_params.cq_off.cqes + m_params.cq_entries * sizeof( struct io_uring_cqe ) };

		const auto length { std::max( sq_len, cq_len ) };

		ptrs.mmap =
			mmap( nullptr, length, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, uring_fd, IORING_OFF_SQ_RING );
	}
	else
	{
		ptrs.length = m_params.sq_off.array + m_params.sq_entries * sizeof( unsigned );
		ptrs.mmap = mmap(
			nullptr, ptrs.length, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, uring_fd, IORING_OFF_SQ_RING );
	}

	const auto& sq_off { m_params.sq_off };
	ptrs.head = reinterpret_cast< unsigned* >( static_cast< std::uint8_t* >( ptrs.mmap ) + sq_off.head );
	ptrs.tail = reinterpret_cast< unsigned* >( static_cast< std::uint8_t* >( ptrs.mmap ) + sq_off.tail );
	ptrs.array = reinterpret_cast< unsigned* >( static_cast< std::uint8_t* >( ptrs.mmap ) + sq_off.array );
	ptrs.mask = reinterpret_cast< unsigned* >( static_cast< std::uint8_t* >( ptrs.mmap ) + sq_off.ring_mask );
	// ptrs.entries = reinterpret_cast< io_uring_sqe* >( static_cast< std::uint8_t* >( ptrs.mmap ) + sq_off.ring_entries );
	ptrs.flags = reinterpret_cast< unsigned* >( static_cast< std::uint8_t* >( ptrs.mmap ) + sq_off.flags );
	ptrs.dropped = reinterpret_cast< unsigned* >( static_cast< std::uint8_t* >( ptrs.mmap ) + sq_off.dropped );
	ptrs.array = reinterpret_cast< unsigned* >( static_cast< std::uint8_t* >( ptrs.mmap ) + sq_off.array );

	return ptrs;
}

IOUring::CommandRingPointers IOUring::setupCommandRing()
{
	CommandRingPointers ptrs {};

	if ( m_params.features & IORING_FEAT_SINGLE_MMAP )
	{
		ptrs.length = 0;
		ptrs.mmap = static_cast< io_uring_cq* >( this->m_submission_ring.mmap );
	}
	else
	{
		ptrs.length = m_params.cq_off.cqes + m_params.cq_entries * sizeof( struct io_uring_cqe );
		ptrs.mmap = static_cast< io_uring_cq* >( mmap(
			nullptr, ptrs.length, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, uring_fd, IORING_OFF_CQ_RING ) );
	}

	ptrs.head = reinterpret_cast< unsigned* >( static_cast< std::uint8_t* >( ptrs.mmap ) + m_params.cq_off.head );
	ptrs.tail = reinterpret_cast< unsigned* >( static_cast< std::uint8_t* >( ptrs.mmap ) + m_params.cq_off.tail );
	ptrs.mask = reinterpret_cast< unsigned* >( static_cast< std::uint8_t* >( ptrs.mmap ) + m_params.cq_off.ring_mask );
	ptrs.overflow =
		reinterpret_cast< unsigned* >( static_cast< std::uint8_t* >( ptrs.mmap ) + m_params.cq_off.overflow );
	ptrs.cqes = reinterpret_cast< io_uring_cqe* >( static_cast< std::uint8_t* >( ptrs.mmap ) + m_params.cq_off.cqes );
	ptrs.flags = reinterpret_cast< unsigned* >( static_cast< std::uint8_t* >( ptrs.mmap ) + m_params.cq_off.flags );

	return ptrs;
}

int IOUring::setupUring( io_uring_params& params )
{
	std::memset( &params, 0, sizeof( params ) );
	// COOP_TASKRUN stops io_uring from interrupting us.
	params.flags = 0;

	static constexpr std::size_t queue_depth { 64 };
	static_assert( queue_depth <= 4096, "Queue depth must be less than 4096" );

	return io_uring_setup( queue_depth, &params );
}

void* IOUring::setupSubmissionEntries() const
{
	return mmap(
		nullptr,
		m_params.sq_entries * sizeof( struct io_uring_sqe ),
		PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_POPULATE,
		uring_fd,
		IORING_OFF_SQES );
}

IOUring::IOUring() :
  uring_fd( setupUring( m_params ) ),
  m_submission_ring( setupSubmissionRing() ),
  m_command_ring( setupCommandRing() ),
  m_submission_entries( static_cast< io_uring_sqe* >( setupSubmissionEntries() ) ),
  io_thread( &ioThread, this, io_run )
{
	m_submission_ring.entries = m_submission_entries;
	// params.sq_thread_idle = 100; // time in ms for kernel thread to then sleep when idle

	// queue depth must be a power of 2

	if ( uring_fd < 0 )
	{
		throw std::runtime_error( format_ns::format( "Failed to setup IO uring, Error code: {}", uring_fd ) );
	}

	instance = this;

	io_run->store( true );
	io_run->notify_all();
}

ReadAwaiter IOUring::send( struct io_uring_sqe sqe )
{
	return ReadAwaiter { this, sqe };
}

IOUring::~IOUring()
{
	this->io_thread.request_stop();
	this->io_thread.join();
	if ( uring_fd > 0 ) close( uring_fd );
}

} // namespace idhan