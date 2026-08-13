#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <vector>

#include "drogon/utils/coroutine.h"
#include "fgl/defines.hpp"

namespace idhan
{

//! Abstract async I/O backend. The server is Linux-only; the sole implementation is linux/IOUringLinux.
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

	// ─── Path operations ──────────────────────────────────────────────────────
	//
	// The std::filesystem equivalents block the calling thread for the duration of the syscall, which
	// on a request thread stalls every other request sharing that event loop. These submit the same
	// work to the ring instead, so the coroutine suspends rather than the thread.
	//
	// Each returns 0 on success or a negative errno, mirroring the completion. Callers decide which
	// errors matter, because there is no single right answer: removing a file that is already gone is
	// usually fine, a failed rename never is. Paths are taken by value so they outlive the suspend --
	// the submission holds a pointer into the string, and the kernel reads it after the caller has
	// already suspended.

	//! unlinkat(2). -ENOENT if the path was not there.
	virtual drogon::Task< int > removeFile( std::filesystem::path path ) = 0;

	//! renameat(2). Atomic, and requires both paths to be on the same filesystem.
	virtual drogon::Task< int > renameFile( std::filesystem::path from, std::filesystem::path to ) = 0;

	//! mkdirat(2) per missing component, like create_directories. -EEXIST is treated as success.
	virtual drogon::Task< int > createDirectories( std::filesystem::path path ) = 0;

	//! statx(2) for the size alone. The error is a negative errno.
	virtual drogon::Task< std::expected< std::uint64_t, int > > fileSize( std::filesystem::path path ) = 0;

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
