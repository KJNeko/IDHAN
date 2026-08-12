#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
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

	virtual drogon::Task< std::vector< std::byte > > read(
		NativeHandle handle,
		std::size_t offset,
		std::size_t len ) = 0;

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
		int m_fd { -1 };
		explicit FileDescriptor( int fd );

		~FileDescriptor();

		// Owns the fd; copying would double-close. Move transfers ownership and resets the source to -1.
		FGL_DELETE_COPY( FileDescriptor );
		FileDescriptor( FileDescriptor&& other ) noexcept;
		FileDescriptor& operator=( FileDescriptor&& other ) noexcept;

		operator int() const;
	};

	FileDescriptor m_fd;
#elif defined( _WIN32 )
	void* m_handle { nullptr }; // HANDLE — void* avoids pulling <windows.h> into this header
#endif

	std::size_t m_size;
	std::filesystem::path m_path;
	bool m_readonly { true };

	[[nodiscard]] IOUring::NativeHandle nativeHandle() const;

	FGL_DELETE_COPY( FileIOUring );

  public:

	constexpr static auto ReadOnly { true };
	constexpr static auto ReadWrite { false };

	explicit FileIOUring( const std::filesystem::path& path, bool readonly = ReadOnly );
	~FileIOUring();
	FGL_DEFAULT_MOVE( FileIOUring );

	[[nodiscard]] std::size_t size() const;
	[[nodiscard]] const std::filesystem::path& path() const;

	[[nodiscard]] drogon::Task< std::vector< std::byte > > read( std::size_t offset, std::size_t len ) const;

	//! Reads the whole file into one owned buffer. Chunked at 512 MiB because io_uring's per-SQE
	//! length field is a __u32, so no single op can cover a file of 4 GiB or more.
	[[nodiscard]] drogon::Task< std::vector< std::byte > > readAll() const;

	[[nodiscard]] drogon::Task< void > write( std::vector< std::byte > data, std::size_t offset = 0 ) const;

	FileIOUring() = delete;
};

} // namespace idhan
