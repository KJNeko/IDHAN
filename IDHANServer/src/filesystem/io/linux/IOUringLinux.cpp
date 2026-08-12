#ifdef __linux__

#include "filesystem/io/linux/IOUringLinux.hpp"

#include <sys/mman.h>

#include <cerrno>
#include <cmath>
#include <cstring>
#include <liburing.h>
#include <malloc.h>
#include <stdexcept>
#include <utility>

#include "drogon/HttpAppFramework.h"
#include "filesystem/io/linux/ReadAwaiter.hpp"
#include "filesystem/io/linux/WriteAwaiter.hpp"
#include "logging/format_ns.hpp"
#include "logging/log.hpp"

namespace idhan
{

// ─── FileDescriptor ───────────────────────────────────────────────────────────

FileIOUring::FileDescriptor::FileDescriptor( const int fd ) : m_fd( fd )
{}

FileIOUring::FileDescriptor::~FileDescriptor()
{
	if ( m_fd > 0 ) close( m_fd );
}

FileIOUring::FileDescriptor::FileDescriptor( FileDescriptor&& other ) noexcept : m_fd( std::exchange( other.m_fd, -1 ) )
{}

FileIOUring::FileDescriptor& FileIOUring::FileDescriptor::operator=( FileDescriptor&& other ) noexcept
{
	if ( this != &other )
	{
		if ( m_fd > 0 ) close( m_fd );
		m_fd = other.m_fd;
		other.m_fd = -1;
	}
	return *this;
}

FileIOUring::FileDescriptor::operator int() const
{
	return m_fd;
}

// ─── FileIOUring (Linux) ──────────────────────────────────────────────────────

FileIOUring::FileIOUring( const std::filesystem::path& path, const bool readonly ) :
  m_fd( open( path.c_str(), ( readonly ? O_RDONLY : ( O_RDWR | O_CREAT ) ), 0666 ) ),
  m_size( std::filesystem::exists( path ) ? std::filesystem::file_size( path ) : 0 ),
  m_path( path ),
  m_readonly( readonly )
{
	if ( static_cast< int >( m_fd ) <= 0 )
		throw std::runtime_error( format_ns::format( "Failed to open file {}", path.string() ) );
}

FileIOUring::~FileIOUring() = default;

IOUring::NativeHandle FileIOUring::nativeHandle() const
{
	if ( static_cast< int >( m_fd ) <= 0 )
		throw std::runtime_error( format_ns::format( "Invalid file descriptor for file {}", m_path.string() ) );

	// Cast via unsigned int to avoid sign-extension of potentially negative (invalid) fds.
	return static_cast< IOUring::NativeHandle >( static_cast< unsigned int >( static_cast< int >( m_fd ) ) );
}

std::size_t FileIOUring::size() const
{
	return m_size;
}

const std::filesystem::path& FileIOUring::path() const
{
	return m_path;
}

drogon::Task< std::vector< std::byte > > FileIOUring::read( const std::size_t offset, std::size_t len ) const
{
	const auto file_max { m_size };
	if ( offset > file_max ) co_return {};
	if ( offset + len > file_max ) len = file_max - offset;

	co_return co_await IOUring::getInstance().read( nativeHandle(), offset, len );
}

drogon::Task< void > FileIOUring::write( const std::vector< std::byte > data, const std::size_t offset ) const
{
	if ( m_readonly ) throw std::runtime_error( "Unable to write: file opened as read-only" );
	co_await IOUring::getInstance().write( nativeHandle(), data, offset );
}

// Copy is deleted (FGL_DELETE_COPY); move is defaulted inline in the header (FGL_DEFAULT_MOVE) and relies on
// FileDescriptor's move ops above to transfer the fd without double-closing.

// ─── IOUring base: Linux dispatch ─────────────────────────────────────────────

static IOUringLinux* g_linux_instance { nullptr };

IOUring& IOUring::getInstance()
{
	if ( !g_linux_instance ) throw std::runtime_error( "IOUring not initialised — call IOUring::init() at startup" );
	return *g_linux_instance;
}

void IOUring::init()
{
	g_linux_instance = new IOUringLinux();
}

// ─── IOUringLinux ─────────────────────────────────────────────────────────────

int IOUringLinux::setupUring()
{
	std::memset( &m_params, 0, sizeof( m_params ) );
	m_params.flags = 0;

	static constexpr std::size_t queue_depth { 64 };
	static_assert( queue_depth <= 4096, "Queue depth must be less than 4096" );

	// io_uring_setup returns the ring fd, or a negative errno on failure. On failure we log why and
	// return the negative value; the caller detects it (uring_fd is signed) and falls back to
	// synchronous pread/pwrite.
	const int ret { io_uring_setup( queue_depth, &m_params ) };

	if ( ret < 0 )
		log::error( "io_uring_setup failed: {} ({}); using synchronous I/O", ret, std::strerror( -ret ) );
	else
		log::debug( "io_uring_setup ok: fd={}, features=0x{:x}", ret, static_cast< unsigned >( m_params.features ) );

	return ret;
}

IOUringLinux::SubmissionRingPointers::~SubmissionRingPointers()
{
	if ( length > 0 ) munmap( mmap, length );
}

IOUringLinux::CommandRingPointers::~CommandRingPointers()
{
	if ( length > 0 ) munmap( mmap, length );
}

IOUringLinux::SubmissionRingPointers IOUringLinux::setupSubmissionRing()
{
	SubmissionRingPointers ptrs {};

	if ( m_params.features & IORING_FEAT_SINGLE_MMAP )
	{
		const auto sq_len { m_params.sq_off.array + m_params.sq_entries * sizeof( unsigned ) };
		const auto cq_len { m_params.cq_off.cqes + m_params.cq_entries * sizeof( io_uring_cqe ) };
		const auto length { std::max( sq_len, cq_len ) };
		ptrs.mmap = mmap(
			nullptr,
			length,
			PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_POPULATE,
			static_cast< int >( uring_fd ),
			IORING_OFF_SQ_RING );
	}
	else
	{
		ptrs.length = m_params.sq_off.array + m_params.sq_entries * sizeof( unsigned );
		ptrs.mmap = mmap(
			nullptr,
			ptrs.length,
			PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_POPULATE,
			static_cast< int >( uring_fd ),
			IORING_OFF_SQ_RING );
	}

	const auto& sq_off { m_params.sq_off };
	ptrs.head = reinterpret_cast< unsigned* >( static_cast< std::uint8_t* >( ptrs.mmap ) + sq_off.head );
	ptrs.tail = reinterpret_cast< unsigned* >( static_cast< std::uint8_t* >( ptrs.mmap ) + sq_off.tail );
	ptrs.array = reinterpret_cast< unsigned* >( static_cast< std::uint8_t* >( ptrs.mmap ) + sq_off.array );
	ptrs.mask = reinterpret_cast< unsigned* >( static_cast< std::uint8_t* >( ptrs.mmap ) + sq_off.ring_mask );
	ptrs.flags = reinterpret_cast< unsigned* >( static_cast< std::uint8_t* >( ptrs.mmap ) + sq_off.flags );
	ptrs.dropped = reinterpret_cast< unsigned* >( static_cast< std::uint8_t* >( ptrs.mmap ) + sq_off.dropped );

	return ptrs;
}

IOUringLinux::CommandRingPointers IOUringLinux::setupCommandRing()
{
	CommandRingPointers ptrs {};

	if ( m_params.features & IORING_FEAT_SINGLE_MMAP )
	{
		ptrs.length = 0;
		ptrs.mmap = m_submission_ring.mmap;
	}
	else
	{
		ptrs.length = m_params.cq_off.cqes + m_params.cq_entries * sizeof( io_uring_cqe );
		ptrs.mmap = mmap(
			nullptr, ptrs.length, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, uring_fd, IORING_OFF_CQ_RING );
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

void* IOUringLinux::setupSubmissionEntries() const
{
	return mmap(
		nullptr,
		m_params.sq_entries * sizeof( io_uring_sqe ),
		PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_POPULATE,
		uring_fd,
		IORING_OFF_SQES );
}

void ioThread( const std::stop_token& token, IOUringLinux* uring, std::shared_ptr< std::atomic< bool > > running )
{
	if ( !uring->m_iouring_setup )
	{
		log::warn( "Exiting IOUringLinux watcher thread: io_uring setup failed" );
		return;
	}

	log::info( "IOUringLinux watcher thread started" );

	if ( running->load() == false ) running->wait( false );

	while ( !token.stop_requested() )
	{
		FGL_ASSERT( uring->uring_fd > 0, "Invalid io_uring fd" );
		const auto ret {
			io_uring_enter( static_cast< unsigned int >( uring->uring_fd ), 0, 1, IORING_ENTER_GETEVENTS, nullptr )
		};

		if ( ret < 0 )
		{
			log::error( "io_uring_enter failed, code: {}", ret );
			std::this_thread::sleep_for( std::chrono::seconds( 5 ) );
			continue;
		}

		unsigned head { io_uring_smp_load_acquire( uring->m_command_ring.head ) };

		while ( head != io_uring_smp_load_acquire( uring->m_command_ring.tail ) )
		{
			const unsigned index { head & *uring->m_command_ring.mask };
			const auto& cqe { uring->m_command_ring.cqes[ index ] };

			// negative cqe.res still carries a valid user_data that must be dispatched (to
			// resume the awaiting coroutine with an exception and free the allocation below) --
			// it must not be `continue`d past
			if ( cqe.res < 0 ) log::error( "io_uring completion error: {}", strerror( -cqe.res ) );

			if ( cqe.user_data == 0 )
			{
				if ( token.stop_requested() ) return;
				log::error( "io_uring completion with null user_data" );
				head++;
				continue;
			}

			auto deleter = []( const IOUringUserData* ptr ) { delete ptr; };
			const auto user_data { std::unique_ptr< IOUringUserData, decltype( deleter ) >(
				reinterpret_cast< IOUringUserData* >( cqe.user_data ), deleter ) };

			switch ( user_data->m_type )
			{
				case IOUringUserData::Type::READ:
					user_data->read_awaiter->complete( cqe.res );
					break;
				case IOUringUserData::Type::WRITE:
					user_data->write_awaiter->complete( cqe.res );
					break;
				default:
					log::error( "IOUringLinux: unknown user_data type" );
					break;
			}

			head++;
		}

		io_uring_smp_store_release( uring->m_command_ring.head, head );
	}
}

void IOUringLinux::sendNop()
{
	std::lock_guard lock { mtx };

	unsigned tail { *m_submission_ring.tail };
	const unsigned index { tail & *m_submission_ring.mask };

	io_uring_sqe sqe {};
	std::memset( &sqe, 0, sizeof( sqe ) );
	io_uring_prep_nop( &sqe );

	m_submission_ring.entries[ index ] = sqe;
	m_submission_ring.array[ index ] = index;
	tail++;

	io_uring_smp_store_release( m_submission_ring.tail, tail );
	notifySubmit( 1 );
}

IOUringLinux& IOUringLinux::getLinuxInstance()
{
	if ( !s_instance ) throw std::runtime_error( "IOUringLinux not initialised" );
	return *s_instance;
}

void IOUringLinux::notifySubmit( const unsigned int count ) const
{
	if ( const auto ret {
			 io_uring_enter( static_cast< unsigned int >( uring_fd ), count, 0, IORING_ENTER_SQ_WAKEUP, nullptr ) };
	     ret < 0 )
		throw std::runtime_error( "io_uring_enter (submit) failed" );
}

IOUringLinux::IOUringLinux() :
  m_params(),
  uring_fd( setupUring() ),
  io_run( std::make_shared< std::atomic< bool > >( false ) ),
  m_submission_ring( setupSubmissionRing() ),
  m_command_ring( setupCommandRing() ),
  m_submission_entries( static_cast< io_uring_sqe* >( setupSubmissionEntries() ) ),
  io_thread( &ioThread, this, io_run )
{
	m_submission_ring.entries = m_submission_entries;
	m_iouring_setup = uring_fd > 0;

	if ( !m_iouring_setup )
	{
		log::error( "io_uring setup failed; falling back to synchronous I/O" );
		log::warn( "If running in Docker, ensure the container has seccomp=unconfined" );
	}

	s_instance = this;
	io_run->store( true );
	io_run->notify_all();
}

IOUringLinux::~IOUringLinux()
{
	sendNop();
	io_thread.request_stop();
	io_thread.join();
	log::info( "Joined IOUringLinux watcher thread" );
	if ( uring_fd > 0 ) close( uring_fd );
}

WriteAwaiter IOUringLinux::sendWrite( const io_uring_sqe& sqe )
{
	return WriteAwaiter { this, sqe };
}

ReadAwaiter IOUringLinux::sendRead( const io_uring_sqe& sqe, std::shared_ptr< std::vector< std::byte > >& data )
{
	return ReadAwaiter { this, sqe, data };
}

drogon::Task< std::vector< std::byte > > IOUringLinux::read(
	const NativeHandle handle,
	const std::size_t offset,
	const std::size_t len )
{
	if ( !m_iouring_setup )
	{
		// Synchronous fallback via pread when io_uring is unavailable
		std::vector< std::byte > data {};
		data.resize( len );
		const auto bytes { pread( static_cast< int >( handle ), data.data(), len, static_cast< off_t >( offset ) ) };
		if ( bytes < 0 ) throw std::runtime_error( format_ns::format( "pread failed: {}", strerror( errno ) ) );
		data.resize( static_cast< std::size_t >( bytes ) );
		co_return data;
	}

	if ( len >= std::numeric_limits< __u32 >::max() )
		throw std::runtime_error( "Read length exceeds io_uring u32 limit" );

	auto buffer_ptr { std::make_shared< std::vector< std::byte > >() };
	buffer_ptr->resize( len );

	io_uring_sqe sqe {};
	std::memset( &sqe, 0, sizeof( sqe ) );
	io_uring_prep_read( &sqe, static_cast< int >( handle ), buffer_ptr->data(), static_cast< __u32 >( len ), offset );

	co_return co_await sendRead( sqe, buffer_ptr );
}

drogon::Task< void > IOUringLinux::write(
	const NativeHandle handle,
	std::vector< std::byte > data,
	const std::size_t offset )
{
	if ( !m_iouring_setup )
	{
		// Synchronous fallback via pwrite when io_uring is unavailable
		const auto bytes {
			pwrite( static_cast< int >( handle ), data.data(), data.size(), static_cast< off_t >( offset ) )
		};
		if ( bytes < 0 ) throw std::runtime_error( format_ns::format( "pwrite failed: {}", strerror( errno ) ) );
		co_return;
	}

	if ( data.size() >= std::numeric_limits< __u32 >::max() )
		throw std::runtime_error( "Write length exceeds io_uring u32 limit" );

	io_uring_sqe sqe {};
	std::memset( &sqe, 0, sizeof( sqe ) );
	io_uring_prep_write( &sqe, static_cast< int >( handle ), data.data(), static_cast< __u32 >( data.size() ), offset );

	co_await sendWrite( sqe );
}

} // namespace idhan

#endif // __linux__
