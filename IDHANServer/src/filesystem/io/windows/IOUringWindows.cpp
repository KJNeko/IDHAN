//
// Created by kj16609 on 7/29/25.
//
// Windows dispatch: IOUring::getInstance() / IOUring::init() and FileIOUring Windows implementation.
//
#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <stdexcept>
#include <windows.h>

#include "filesystem/io/IOUring.hpp"
#include "filesystem/io/windows/IOUringW10.hpp"
#include "filesystem/io/windows/IOUringW11.hpp"
#include "logging/format_ns.hpp"
#include "logging/log.hpp"

namespace idhan
{

// ─── IOUring base: Windows dispatch ──────────────────────────────────────────

static IOUring* g_win_instance { nullptr };

IOUring& IOUring::getInstance()
{
	if ( !g_win_instance ) throw std::runtime_error( "IOUring not initialised — call IOUring::init() at startup" );
	return *g_win_instance;
}

void IOUring::init()
{
	if ( IOUringW11::isAvailable() )
	{
		log::info( "Windows IoRing (W11) detected, using IOUringW11 backend" );
		g_win_instance = new IOUringW11();
	}
	else
	{
		log::info( "IoRing unavailable, using IOUringW10 (IOCP) backend" );
		g_win_instance = new IOUringW10();
	}
}

// ─── FileIOUring (Windows) ────────────────────────────────────────────────────

FileIOUring::FileIOUring( const std::filesystem::path& path, const bool readonly ) :
  m_size( 0 ),
  m_path( path ),
  m_readonly( readonly )
{
	const DWORD access { readonly ? GENERIC_READ : ( GENERIC_READ | GENERIC_WRITE ) };
	const DWORD share { FILE_SHARE_READ };
	const DWORD disposition { readonly ? OPEN_EXISTING : OPEN_ALWAYS };
	// FILE_FLAG_OVERLAPPED is required for IOCP async I/O.
	// FILE_FLAG_SEQUENTIAL_SCAN hints to the prefetcher.
	const DWORD flags { FILE_FLAG_OVERLAPPED | FILE_FLAG_SEQUENTIAL_SCAN };

	m_handle = CreateFileW( path.wstring().c_str(), access, share, nullptr, disposition, flags, nullptr );

	if ( m_handle == INVALID_HANDLE_VALUE || m_handle == nullptr )
		throw std::runtime_error( format_ns::format( "Failed to open file {}", path.string() ) );

	// Get file size
	LARGE_INTEGER file_size {};
	if ( GetFileSizeEx( m_handle, &file_size ) ) m_size = static_cast< std::size_t >( file_size.QuadPart );

	// Associate with the backend (required by IOCP; no-op for IoRing)
	IOUring::getInstance().associateHandle( nativeHandle() );
}

FileIOUring::~FileIOUring()
{
	if ( m_handle && m_handle != INVALID_HANDLE_VALUE ) CloseHandle( m_handle );
}

IOUring::NativeHandle FileIOUring::nativeHandle() const
{
	return reinterpret_cast< IOUring::NativeHandle >( m_handle );
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

FileIOUring::FileIOUring( const FileIOUring& other ) :
  m_handle( nullptr ),
  m_size( other.m_size ),
  m_path( other.m_path ),
  m_readonly( other.m_readonly )
{
	// Duplicate the handle so both instances are independently closeable
	if ( other.m_handle && other.m_handle != INVALID_HANDLE_VALUE )
	{
		if (
			!DuplicateHandle(
				GetCurrentProcess(), other.m_handle, GetCurrentProcess(), &m_handle, 0, FALSE, DUPLICATE_SAME_ACCESS ) )
		{
			throw std::runtime_error( "FileIOUring copy: DuplicateHandle failed" );
		}
		IOUring::getInstance().associateHandle( nativeHandle() );
	}
}

FileIOUring& FileIOUring::operator=( const FileIOUring& other )
{
	if ( this == &other ) return *this;
	FileIOUring tmp { other };
	std::swap( m_handle, tmp.m_handle );
	m_size = other.m_size;
	m_path = other.m_path;
	m_readonly = other.m_readonly;
	return *this;
}

FileIOUring::FileIOUring( FileIOUring&& other ) noexcept :
  m_handle( std::exchange( other.m_handle, nullptr ) ),
  m_size( other.m_size ),
  m_path( std::move( other.m_path ) ),
  m_readonly( other.m_readonly )
{}

FileIOUring& FileIOUring::operator=( FileIOUring&& other ) noexcept
{
	if ( this == &other ) return *this;
	if ( m_handle && m_handle != INVALID_HANDLE_VALUE ) CloseHandle( m_handle );
	m_handle = std::exchange( other.m_handle, nullptr );
	m_size = other.m_size;
	m_path = std::move( other.m_path );
	m_readonly = other.m_readonly;
	return *this;
}

} // namespace idhan

#endif // _WIN32
