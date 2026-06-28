//
// Created by kj16609 on 7/29/25.
//
#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <utility>
#include <vector>

#include "drogon/utils/coroutine.h"
#include "fgl/defines.hpp"

namespace idhan
{

//! Abstract async I/O backend. Platform implementations: linux/IOUringLinux, windows/IOUringW10, windows/IOUringW11.
class IOUring
{
  public:

	//! Platform-agnostic file handle. Stores int (Linux fd) or HANDLE (Windows, pointer-sized).
	using NativeHandle = std::uintptr_t;

	virtual drogon::Task< std::vector< std::byte > >
		read( NativeHandle handle, std::size_t offset, std::size_t len ) = 0;

	virtual drogon::Task< void > write( NativeHandle handle, std::vector< std::byte > data, std::size_t offset ) = 0;

	//! Associates a file handle with this backend. Required by IOCP; no-op for other backends.
	virtual void associateHandle( [[maybe_unused]] NativeHandle handle ) {}

	virtual ~IOUring() = default;

	IOUring() = default;
	FGL_DELETE_COPY( IOUring );
	FGL_DELETE_MOVE( IOUring );

	//! Returns the active backend. Must call init() before first use.
	static IOUring& getInstance();

	//! Selects and initialises the backend appropriate for the current platform/OS version.
	static void init();
};

//! File handle wrapper. Provides async read/write and mmap via the active IOUring backend.
class [[nodiscard]] FileIOUring
{
#ifdef __linux__
	struct FileDescriptor
	{
		std::shared_ptr< int > m_fd;
		explicit FileDescriptor( int fd );
		operator int() const;
	};

	FileDescriptor m_fd;
	void* m_mmap_ptr { nullptr };
#elif defined( _WIN32 )
	void* m_handle { nullptr };                    // HANDLE — void* avoids pulling <windows.h> into this header
	mutable std::vector< std::byte > m_mmap_buffer {}; // populated lazily on first mmapReadOnly()
#endif

	std::size_t m_size;
	std::filesystem::path m_path;
	bool m_readonly;

	[[nodiscard]] IOUring::NativeHandle nativeHandle() const;

  public:

	constexpr static auto ReadOnly { true };
	constexpr static auto ReadWrite { false };

	explicit FileIOUring( const std::filesystem::path& path, bool readonly = ReadOnly );
	~FileIOUring();

	[[nodiscard]] std::size_t size() const;
	[[nodiscard]] const std::filesystem::path& path() const;

	[[nodiscard]] drogon::Task< std::vector< std::byte > > read( std::size_t offset, std::size_t len ) const;
	[[nodiscard]] drogon::Task< void > write( std::vector< std::byte > data, std::size_t offset = 0 ) const;

	//! Memory-maps the file read-only. Lifetime of returned pointer tied to this FileIOUring instance.
	[[nodiscard]] std::pair< void*, std::size_t > mmapReadOnly();

	FileIOUring() = delete;
	[[nodiscard]] FileIOUring( const FileIOUring& );
	[[nodiscard]] FileIOUring& operator=( const FileIOUring& );
	[[nodiscard]] FileIOUring( FileIOUring&& ) noexcept;
	[[nodiscard]] FileIOUring& operator=( FileIOUring&& ) noexcept;
};

} // namespace idhan
