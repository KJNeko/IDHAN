//
// Created by kj16609 on 7/28/26.
//
#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>
#include <string>

#include "ModuleBase.hpp"
#include "UniqueFd.hpp"

namespace idhan::ipc
{

//! A read-only block of bytes shareable with another process, backed by a sealed anonymous memfd.
/** This is how a file reaches a module. The host copies the file's bytes into an anonymous memory
 *  object, seals it against writing and resizing, and passes the descriptor over the worker socket;
 *  the worker maps it and hands the module a plain pointer and size.
 *
 *  The point of the memfd is what it does *not* carry. A worker that received the real file
 *  descriptor could readlink("/proc/self/fd/N") and learn the cluster path, the filename, and by
 *  extension the layout of the whole store. A memfd resolves to "/memfd:idhan-blob (deleted)" and
 *  nothing else, so a module sees the bytes it was asked to process and has no handle on where they
 *  came from. The seals mean it also cannot modify what the host still has mapped.
 *
 *  The cost is one kernel-side copy per blob (copy_file_range, so the bytes never enter userspace).
 *  Passing the file descriptor directly would avoid even that, but it is exactly the leak above. */
class Blob
{
	UniqueFd m_fd {};
	//! Held as void* rather than as the const byte pointer callers see, so unmapping never has to
	//! cast the constness back off.
	void* m_mapping { nullptr };
	std::size_t m_size { 0 };

	Blob( UniqueFd fd, void* mapping, std::size_t size );

  public:

	Blob() = default;
	~Blob();

	Blob( const Blob& ) = delete;
	Blob& operator=( const Blob& ) = delete;

	Blob( Blob&& other ) noexcept;
	Blob& operator=( Blob&& other ) noexcept;

	//! Creates a blob holding a copy of \p path's contents.
	/** The copy is done with copy_file_range, falling back to sendfile and then to a read/write loop
	 *  on kernels or filesystems that refuse it, so the bytes are never materialised in this
	 *  process's heap. */
	[[nodiscard]] static std::expected< Blob, std::string > fromFile( const std::filesystem::path& path );

	//! Creates a blob holding a copy of \p bytes. Used for data that is already in memory, e.g. a
	//! request body or a buffer a module produced and asked the host to re-dispatch.
	[[nodiscard]] static std::expected< Blob, std::string > fromBytes( std::span< const std::byte > bytes );

	//! Maps a descriptor received over the socket. Takes ownership of \p fd either way.
	[[nodiscard]] static std::expected< Blob, std::string > adopt( UniqueFd fd );

	//! The bytes, as the non-owning view modules are given. Empty (and null) for a zero-length blob.
	[[nodiscard]] data_view view() const
	{
		return data_view { static_cast< const std::uint8_t* >( m_mapping ), m_size };
	}

	[[nodiscard]] std::span< const std::byte > bytes() const
	{
		return std::span< const std::byte > { static_cast< const std::byte* >( m_mapping ), m_size };
	}

	//! The descriptor to attach to an outgoing frame. Ownership stays with the blob.
	[[nodiscard]] int fd() const { return m_fd.get(); }

	[[nodiscard]] std::size_t size() const { return m_size; }

	[[nodiscard]] bool valid() const { return static_cast< bool >( m_fd ); }
};

} // namespace idhan::ipc
