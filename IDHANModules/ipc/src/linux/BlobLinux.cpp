//
// Created by kj16609 on 7/28/26.
//
#ifdef __linux__

#include <sys/mman.h>
#include <sys/sendfile.h>
#include <sys/stat.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <format>
#include <unistd.h>
#include <utility>

#include "ipc/Blob.hpp"

namespace idhan::ipc
{

namespace
{

//! Seals applied to every blob before it leaves this process.
/** F_SEAL_WRITE is the one that matters: it makes the mapping the worker receives genuinely
 *  read-only, so a module cannot scribble over bytes the host is still using. The others close the
 *  ways a resize could invalidate a mapping out from under either side, and F_SEAL_SEAL stops the
 *  receiver relaxing any of it. */
constexpr unsigned int BLOB_SEALS { F_SEAL_WRITE | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL };

[[nodiscard]] std::string errnoMessage( const char* const what )
{
	return std::format( "{}: {}", what, std::strerror( errno ) );
}

//! Maps a sealed memfd read-only. A zero-length blob maps to nothing: mmap rejects a length of 0,
//! and a null pointer with a zero size is exactly the empty view callers expect.
[[nodiscard]] std::expected< void*, std::string > mapReadOnly( const int fd, const std::size_t size )
{
	if ( size == 0 ) return nullptr;

	void* const mapping { ::mmap( nullptr, size, PROT_READ, MAP_SHARED, fd, 0 ) };
	if ( mapping == MAP_FAILED ) return std::unexpected( errnoMessage( "mmap of blob failed" ) );

	return mapping;
}

//! Creates an anonymous, sealable, close-on-exec memory object sized to \p size.
/** Close-on-exec matters here: the host forks and execs workers constantly, and a blob that stayed
 *  open across an unrelated exec would be both a leak and a way for one worker to see another's
 *  data. The descriptor is handed to the intended worker explicitly via SCM_RIGHTS instead. */
[[nodiscard]] std::expected< UniqueFd, std::string > createMemfd( const std::size_t size )
{
	UniqueFd fd { ::memfd_create( "idhan-blob", MFD_CLOEXEC | MFD_ALLOW_SEALING ) };
	if ( !fd ) return std::unexpected( errnoMessage( "memfd_create failed" ) );

	if ( size > 0 && ::ftruncate( fd.get(), static_cast< off_t >( size ) ) != 0 )
		return std::unexpected( errnoMessage( "ftruncate of blob failed" ) );

	return fd;
}

[[nodiscard]] std::expected< void, std::string > sealBlob( const int fd )
{
	if ( ::fcntl( fd, F_ADD_SEALS, BLOB_SEALS ) != 0 ) return std::unexpected( errnoMessage( "sealing blob failed" ) );

	return {};
}

//! Copies \p size bytes from \p in to \p out entirely inside the kernel.
/** copy_file_range is the fast path but is refused in enough situations to need fallbacks:
 *  cross-filesystem copies were rejected outright before Linux 5.3, and both it and sendfile can
 *  return short counts that have to be looped over rather than treated as errors. The final
 *  read/write loop always works and is only reached on kernels or mounts that block the others. */
[[nodiscard]] std::expected< void, std::string > copyIntoBlob( const int in, const int out, const std::size_t size )
{
	std::size_t copied { 0 };
	bool copy_file_range_usable { true };

	while ( copied < size )
	{
		const std::size_t remaining { size - copied };

		if ( copy_file_range_usable )
		{
			off_t off_in { static_cast< off_t >( copied ) };
			off_t off_out { static_cast< off_t >( copied ) };
			const ssize_t result { ::copy_file_range( in, &off_in, out, &off_out, remaining, 0 ) };

			if ( result > 0 )
			{
				copied += static_cast< std::size_t >( result );
				continue;
			}

			// A zero return means the source ended early -- the file shrank since we stat'd it.
			if ( result == 0 ) return std::unexpected( "source file shrank while being copied into a blob" );

			if ( errno != EXDEV && errno != EINVAL && errno != ENOSYS && errno != EOPNOTSUPP )
				return std::unexpected( errnoMessage( "copy_file_range failed" ) );

			copy_file_range_usable = false;
			continue;
		}

		off_t off_in { static_cast< off_t >( copied ) };
		const ssize_t sent { ::sendfile( out, in, &off_in, remaining ) };

		if ( sent > 0 )
		{
			copied += static_cast< std::size_t >( sent );
			continue;
		}

		if ( sent == 0 ) return std::unexpected( "source file shrank while being copied into a blob" );

		if ( errno == EINTR ) continue;

		// Last resort: bounce through a stack buffer.
		constexpr std::size_t CHUNK { 256 * 1024 };
		std::array< std::byte, CHUNK > buffer {};

		while ( copied < size )
		{
			const std::size_t want { std::min( CHUNK, size - copied ) };
			const ssize_t got { ::pread( in, buffer.data(), want, static_cast< off_t >( copied ) ) };

			if ( got < 0 )
			{
				if ( errno == EINTR ) continue;
				return std::unexpected( errnoMessage( "reading source file failed" ) );
			}
			if ( got == 0 ) return std::unexpected( "source file shrank while being copied into a blob" );

			std::size_t written { 0 };
			while ( written < static_cast< std::size_t >( got ) )
			{
				const ssize_t put { ::pwrite(
					out,
					buffer.data() + written,
					static_cast< std::size_t >( got ) - written,
					static_cast< off_t >( copied + written ) ) };

				if ( put < 0 )
				{
					if ( errno == EINTR ) continue;
					return std::unexpected( errnoMessage( "writing blob failed" ) );
				}

				written += static_cast< std::size_t >( put );
			}

			copied += written;
		}
	}

	return {};
}

} // namespace

Blob::Blob( UniqueFd fd, void* const mapping, const std::size_t size ) :
  m_fd( std::move( fd ) ),
  m_mapping( mapping ),
  m_size( size )
{}

Blob::~Blob()
{
	if ( m_mapping != nullptr && m_size > 0 ) ::munmap( m_mapping, m_size );
}

Blob::Blob( Blob&& other ) noexcept :
  m_fd( std::move( other.m_fd ) ),
  m_mapping( std::exchange( other.m_mapping, nullptr ) ),
  m_size( std::exchange( other.m_size, 0 ) )
{}

Blob& Blob::operator=( Blob&& other ) noexcept
{
	if ( this != &other )
	{
		if ( m_mapping != nullptr && m_size > 0 ) ::munmap( m_mapping, m_size );

		m_fd = std::move( other.m_fd );
		m_mapping = std::exchange( other.m_mapping, nullptr );
		m_size = std::exchange( other.m_size, 0 );
	}

	return *this;
}

std::expected< Blob, std::string > Blob::fromFile( const std::filesystem::path& path )
{
	const UniqueFd source { ::open( path.c_str(), O_RDONLY | O_CLOEXEC ) };
	if ( !source ) return std::unexpected( errnoMessage( std::format( "opening {}", path.string() ).c_str() ) );

	struct stat info {};
	if ( ::fstat( source.get(), &info ) != 0 )
		return std::unexpected( errnoMessage( std::format( "stat of {}", path.string() ).c_str() ) );

	if ( !S_ISREG( info.st_mode ) ) return std::unexpected( std::format( "{} is not a regular file", path.string() ) );

	const auto size { static_cast< std::size_t >( info.st_size ) };

	auto fd { createMemfd( size ) };
	if ( !fd ) return std::unexpected( fd.error() );

	if ( size > 0 )
	{
		const auto copied { copyIntoBlob( source.get(), fd->get(), size ) };
		if ( !copied ) return std::unexpected( copied.error() );
	}

	const auto sealed { sealBlob( fd->get() ) };
	if ( !sealed ) return std::unexpected( sealed.error() );

	const auto mapping { mapReadOnly( fd->get(), size ) };
	if ( !mapping ) return std::unexpected( mapping.error() );

	return Blob { std::move( *fd ), *mapping, size };
}

std::expected< Blob, std::string > Blob::fromBytes( const std::span< const std::byte > bytes )
{
	auto fd { createMemfd( bytes.size() ) };
	if ( !fd ) return std::unexpected( fd.error() );

	// Written with pwrite rather than through a mapping on purpose: F_SEAL_WRITE is refused while a
	// writable mapping of the object exists, so filling it by mmap would mean unmapping before
	// sealing and re-mapping after.
	std::size_t written { 0 };
	while ( written < bytes.size() )
	{
		const ssize_t put {
			::pwrite( fd->get(), bytes.data() + written, bytes.size() - written, static_cast< off_t >( written ) )
		};

		if ( put < 0 )
		{
			if ( errno == EINTR ) continue;
			return std::unexpected( errnoMessage( "writing blob failed" ) );
		}

		written += static_cast< std::size_t >( put );
	}

	const auto sealed { sealBlob( fd->get() ) };
	if ( !sealed ) return std::unexpected( sealed.error() );

	const auto mapping { mapReadOnly( fd->get(), bytes.size() ) };
	if ( !mapping ) return std::unexpected( mapping.error() );

	return Blob { std::move( *fd ), *mapping, bytes.size() };
}

std::expected< Blob, std::string > Blob::adopt( UniqueFd fd )
{
	if ( !fd ) return std::unexpected( std::string { "cannot adopt an invalid descriptor as a blob" } );

	struct stat info {};
	if ( ::fstat( fd.get(), &info ) != 0 ) return std::unexpected( errnoMessage( "stat of received blob" ) );

	const auto size { static_cast< std::size_t >( info.st_size ) };

	// Deliberately not verified against the expected seals. The receiver is the untrusted side of
	// this boundary, not the sender: refusing an unsealed descriptor here would protect a worker
	// from the host, which is backwards.
	const auto mapping { mapReadOnly( fd.get(), size ) };
	if ( !mapping ) return std::unexpected( mapping.error() );

	return Blob { std::move( fd ), *mapping, size };
}

} // namespace idhan::ipc

#endif
