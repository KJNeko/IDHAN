//
// Created by kj16609 on 7/29/25.
//
#include "IOUring.hpp"

#include <sys/mman.h>

#include <cmath>
#include <fstream>
#include <liburing.h>
#include <stdexcept>

#include "ReadAwaiter.hpp"
#include "WriteAwaiter.hpp"
#include "drogon/HttpAppFramework.h"
#include "logging/format_ns.hpp"
#include "logging/log.hpp"
#include "threading/ImmedientTask.hpp"

namespace idhan
{

void fileDescriptorDeleter( const int* fd )
{
	close( *fd );
	delete fd;
}

FileIOUring::FileDescriptor::FileDescriptor( const int fd ) :
  m_fd( std::shared_ptr< int > { new int( fd ), fileDescriptorDeleter } )
{}

FileIOUring::FileDescriptor::operator int() const
{
	return *m_fd;
}

FileIOUring::FileIOUring( const std::filesystem::path& path ) :
  m_fd( open( path.c_str(), O_RDWR | O_CREAT, 0666 ) ),
  m_size( std::filesystem::file_size( path ) ),
  m_path( path )
{
	if ( m_fd <= 0 ) throw std::runtime_error( format_ns::format( "Failed to open file {}", path.string() ) );
}

FileIOUring::~FileIOUring()
{
	if ( m_mmap_ptr ) munmap( m_mmap_ptr, m_size );
}

std::size_t FileIOUring::size() const
{
	return m_size;
}

const std::filesystem::path& FileIOUring::path() const
{
	return m_path;
}

coro::ImmedientTask< std::vector< std::byte > > FileIOUring::readAll() const
{
	const auto file_size { std::filesystem::file_size( m_path ) };
	co_return co_await read( 0, file_size );
}

coro::ImmedientTask< std::vector< std::byte > > FileIOUring::read( const std::size_t offset, const std::size_t len )
	const
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

	auto buffer_ptr { std::make_shared< std::vector< std::byte > >() };
	buffer_ptr->resize( len );

	io_uring_prep_read( &sqe, m_fd, buffer_ptr->data(), static_cast< __u32 >( len ), offset );

	co_return co_await uring.sendRead( sqe, buffer_ptr );
}

drogon::Task< void > FileIOUring::write( const std::vector< std::byte > data, const std::size_t offset ) const
{
	auto& uring { IOUring::getInstance() };

	if ( uring.m_iouring_setup == false ) co_await fallbackWrite( data, offset );

	const auto len { data.size() };
	if ( m_fd <= 0 ) throw std::runtime_error( "FileIOUring::write: Invalid file descriptor" );
	if ( len >= std::numeric_limits< __u32 >::max() )
		throw std::runtime_error(
			format_ns::format(
				"FileIOUring::write: len > std::numeric_limits<unsigned>::max() ({} >= {}) Tell the Dev!",
				len,
				std::numeric_limits< __u32 >::max() ) );

	io_uring_sqe sqe {};
	std::memset( &sqe, 0, sizeof( sqe ) );

	io_uring_prep_write( &sqe, m_fd, data.data(), static_cast< __u32 >( len ), offset );

	co_await uring.sendWrite( sqe );

	co_return;
}

drogon::Task< std::vector< std::byte > > FileIOUring::fallbackRead( const std::size_t offset, const std::size_t len )
	const
{
	if ( std::ifstream ifs( m_path, std::ios::binary ); ifs )
	{
		std::vector< std::byte > data {};

		data.reserve( len );

		if ( offset > 0 ) ifs.seekg( offset );

		ifs.read( reinterpret_cast< char* >( data.data() ), len );

		co_return data;
	}

	throw std::
		runtime_error( format_ns::format( "FileIOUring::fallbackRead: Failed to open file {}", m_path.string() ) );
}

drogon::Task< void > FileIOUring::fallbackWrite( const std::vector< std::byte > data, const std::size_t size ) const
{
	if ( std::ofstream ofs( m_path, std::ios::trunc | std::ios::binary ); ofs )
	{
		ofs.write( reinterpret_cast< const std::ostream::char_type* >( data.data() ), size );
	}
	else
		throw std::
			runtime_error( format_ns::format( "FileIOUring::fallbackWrite: Failed to open file {}", m_path.string() ) );

	co_return;
}

std::pair< void*, std::size_t > FileIOUring::mmap()
{
	void* ptr = ::mmap( nullptr, size(), PROT_READ, MAP_SHARED, m_fd, 0 );
	m_mmap_ptr = ptr;
	return std::make_pair( ptr, size() );
}

FileIOUring::FileIOUring( const FileIOUring& ) = default;
FileIOUring& FileIOUring::operator=( const FileIOUring& ) = default;
FileIOUring::FileIOUring( FileIOUring&& ) noexcept = default;
FileIOUring& FileIOUring::operator=( FileIOUring&& ) noexcept = default;

int IOUring::setupUring()
{
	std::memset( &m_params, 0, sizeof( m_params ) );
	// COOP_TASKRUN stops io_uring from interrupting us.
	m_params.flags = 0;

	static constexpr std::size_t queue_depth { 64 };
	static_assert( queue_depth <= 4096, "Queue depth must be less than 4096" );

	return io_uring_setup( queue_depth, &m_params );
}

IOUring::SubmissionRingPointers IOUring::setupSubmissionRing()
{
	SubmissionRingPointers ptrs {};

	if ( m_params.features & IORING_FEAT_SINGLE_MMAP )
	{
		const auto sq_len { m_params.sq_off.array + m_params.sq_entries * sizeof( unsigned ) };
		const auto cq_len { m_params.cq_off.cqes + m_params.cq_entries * sizeof( io_uring_cqe ) };

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
		ptrs.length = m_params.cq_off.cqes + m_params.cq_entries * sizeof( io_uring_cqe );
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

void* IOUring::setupSubmissionEntries() const
{
	return mmap(
		nullptr,
		m_params.sq_entries * sizeof( io_uring_sqe ),
		PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_POPULATE,
		uring_fd,
		IORING_OFF_SQES );
}

void ioThread( const std::stop_token& token, IOUring* uring, std::shared_ptr< std::atomic< bool > > running )
{
	if ( !uring->m_iouring_setup )
	{
		log::warn( "Exiting iouring watcher thread due to invalid io_uring setup" );
		return;
	}

	log::info( "IOUring watcher thread started" );

	const auto min_complete { 1 };

	if ( running->load() == false ) running->wait( false );

	while ( !token.stop_requested() )
	{
		// We can check the CQ ring tail for things being inserted.
		FGL_ASSERT( uring->uring_fd > 0, "Invalid io_uring fd" );
		const auto ret { io_uring_enter( uring->uring_fd, 0, min_complete, IORING_ENTER_GETEVENTS, nullptr ) };

		if ( ret < 0 )
		{
			log::error( "Failed to enter io_uring, Error code: {}", ret );
			std::this_thread::sleep_for( std::chrono::seconds( 5 ) );
			continue;
		}

		unsigned head { io_uring_smp_load_acquire( uring->m_command_ring.head ) };

		while ( head != io_uring_smp_load_acquire( uring->m_command_ring.tail ) )
		{
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
				if ( token.stop_requested() ) return;
				log::error( "Awaiter was not given" );
				head++;
				continue;
			}

			auto deleter = []( const IOUringUserData* ptr ) -> void { delete ptr; };

			const auto user_data { std::unique_ptr<
				IOUringUserData,
				decltype( deleter ) >( reinterpret_cast< IOUringUserData* >( cqe.user_data ), deleter ) };

			switch ( user_data->m_type )
			{
				case IOUringUserData::Type::READ:
					{
						const auto& read_awaiter { user_data->read_awaiter };

						// auto* buffer { reinterpret_cast< std::byte* >( read_awaiter->m_sqe.addr ) };

						read_awaiter->complete( cqe.res );
						break;
					}
				case IOUringUserData::Type::WRITE:
					{
						const auto& write_awaiter { user_data->write_awaiter };
						write_awaiter->complete( cqe.res );
						break;
					}
				default:
					{
						log::error( "IOUring: Unknown user data type" );
						break;
					}
			}

			head++;
		}

		io_uring_smp_store_release( uring->m_command_ring.head, head );
	}
}

void IOUring::sendNop()
{
	std::lock_guard lock { mtx };

	unsigned tail = *m_submission_ring.tail;
	unsigned index = tail & *m_submission_ring.mask;

	io_uring_sqe sqe {};
	std::memset( &sqe, 0, sizeof( sqe ) );
	io_uring_prep_nop( &sqe );

	m_submission_ring.entries[ index ] = sqe;
	m_submission_ring.array[ index ] = index;
	tail++;

	io_uring_smp_store_release( m_submission_ring.tail, tail );

	notifySubmit( 1 );
}

IOUring& IOUring::getInstance()
{
	if ( !instance ) throw std::runtime_error( "IOUring instance not initialized" );
	return *instance;
}

void IOUring::notifySubmit( unsigned int count ) const
{
	if ( auto ret = io_uring_enter( uring_fd, count, 0, IORING_ENTER_SQ_WAKEUP, nullptr ); ret < 0 )
	{
		throw std::runtime_error( "Failed to enter io_uring" );
	}
}

IOUring::IOUring() :
  uring_fd( setupUring() ),
  m_submission_ring( setupSubmissionRing() ),
  m_command_ring( setupCommandRing() ),
  m_submission_entries( static_cast< io_uring_sqe* >( setupSubmissionEntries() ) ),
  io_thread( &ioThread, this, io_run )
{
	m_submission_ring.entries = m_submission_entries;
	// params.sq_thread_idle = 100; // time in ms for kernel thread to then sleep when idle

	// queue depth must be a power of 2

	m_iouring_setup = uring_fd > 0;

	if ( m_iouring_setup == false )
	{
		log::error( "Failed to set up IOUring, Falling back to slower read/write system" );
		log::warn( "If you are running a docker container, Ensure IDHAN's contained has `seccomp=unconfined`" );
	}

	instance = this;

	io_run->store( true );
	io_run->notify_all();
}

WriteAwaiter IOUring::sendWrite( const io_uring_sqe& sqe )
{
	return WriteAwaiter { this, sqe };
}

ReadAwaiter IOUring::sendRead( const io_uring_sqe& sqe, std::shared_ptr< std::vector< std::byte > >& data )
{
	return ReadAwaiter { this, sqe, data };
}

IOUring::~IOUring()
{
	this->sendNop();
	this->io_thread.request_stop();
	this->io_thread.join();
	log::info( "Joined io_uring watcher thread" );
	if ( uring_fd > 0 ) close( uring_fd );
}

} // namespace idhan