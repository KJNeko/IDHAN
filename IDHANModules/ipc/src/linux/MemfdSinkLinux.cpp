#ifdef __linux__

#include <sys/mman.h>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <format>
#include <new>
#include <unistd.h>

#include "idhan/errnoMessage.hpp"
#include "ipc/MemfdSink.hpp"

namespace idhan::ipc
{

//! Seals applied once the output is complete. Identical in intent to Blob's: the host maps what
//! comes back, and nothing should be able to change it afterwards.
constexpr unsigned int SINK_SEALS { F_SEAL_WRITE | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL };

std::expected< std::unique_ptr< MemfdSink >, std::string > MemfdSink::create()
{
	std::unique_ptr< MemfdSink > sink { new ( std::nothrow ) MemfdSink {} };
	if ( !sink ) return std::unexpected( "could not allocate a MemfdSink" );

	sink->m_fd.reset( ::memfd_create( "idhan-sink", MFD_CLOEXEC | MFD_ALLOW_SEALING ) );
	if ( !sink->m_fd ) return std::unexpected( errnoMessage( "memfd_create failed" ) );

	return sink;
}

std::expected< void, ModuleError > MemfdSink::reserve( const std::size_t bytes )
{
	// A reservation is a hint about the expected total, not a limit, so shrinking below what has
	// already been written is simply ignored rather than treated as an error.
	if ( bytes <= m_reserved ) return {};

	if ( ::ftruncate( m_fd.get(), static_cast< off_t >( bytes ) ) != 0 )
		return std::unexpected( ModuleError { errnoMessage( "ftruncate of sink failed" ) } );

	m_reserved = bytes;

	return {};
}

std::expected< void, ModuleError > MemfdSink::write( const std::span< const std::byte > bytes )
{
	std::size_t sent { 0 };

	while ( sent < bytes.size() )
	{
		const ssize_t result {
			::pwrite( m_fd.get(), bytes.data() + sent, bytes.size() - sent, static_cast< off_t >( m_written + sent ) )
		};

		if ( result > 0 )
		{
			sent += static_cast< std::size_t >( result );
			continue;
		}

		if ( result < 0 && errno == EINTR ) continue;

		// A zero return with bytes still outstanding means no progress is being made; looping would
		// spin forever.
		return std::unexpected(
			ModuleError { result == 0 ? std::string { "write to sink made no progress" } :
		                                errnoMessage( "write to sink failed" ) } );
	}

	m_written += sent;

	return {};
}

std::expected< UniqueFd, std::string > MemfdSink::seal()
{
	// Trim a reservation the module did not fill, so trailing zeroes are not mistaken for data.
	if ( m_reserved != m_written && ::ftruncate( m_fd.get(), static_cast< off_t >( m_written ) ) != 0 )
		return std::unexpected( errnoMessage( "ftruncate of finished sink failed" ) );

	if ( ::fcntl( m_fd.get(), F_ADD_SEALS, SINK_SEALS ) != 0 )
		return std::unexpected( errnoMessage( "sealing sink failed" ) );

	m_reserved = m_written;

	return std::move( m_fd );
}

} // namespace idhan::ipc

#endif // __linux__
