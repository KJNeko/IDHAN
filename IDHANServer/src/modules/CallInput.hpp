#pragma once

#include <json/value.h>

#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>

#include "ipc/Blob.hpp"
#include "ipc/UniqueFd.hpp"

namespace idhan::modules
{

//! The server's side of a module call's input: the bytes, and how to put them in front of a worker.
/** The out-of-process analogue of ModuleFile. A module sees a handle it can read; this is the other
 *  end of it.
 *
 *  What crosses is a descriptor the worker maps read-only. For a record that descriptor is the
 *  cluster file itself, so nothing is copied and nothing is resident that a module has not actually
 *  touched -- a thumbnailer decoding one frame of a 4 GiB video faults in a few megabytes of page
 *  cache and no more. For bytes that were never a file (an HTTP request body, a buffer a module
 *  produced) it is a sealed memfd, because there is nothing else it could be.
 *
 *  The worker cannot tell the two apart: mmap does not care what backs a descriptor. */
class CallInput
{
	//! The descriptor the worker will map. A record's O_RDONLY file, or the memfd below.
	ipc::UniqueFd m_file {};

	//! Held only when the bytes had to be staged into anonymous memory. Owns the descriptor in that
	//! case, and gives the server a mapping to scan for a MIME when it has no other way to know one.
	ipc::Blob m_blob {};

	std::size_t m_size { 0 };

	CallInput() = default;

  public:

	~CallInput() = default;

	CallInput( const CallInput& ) = delete;
	CallInput& operator=( const CallInput& ) = delete;
	CallInput( CallInput&& ) noexcept = default;
	CallInput& operator=( CallInput&& ) noexcept = default;

	//! Prepares a record's file on disk as a call input. Opens it; does not read it.
	[[nodiscard]] static std::expected< CallInput, std::string > forPath( const std::filesystem::path& path );

	//! Prepares bytes the server already holds -- a callback payload, a request body.
	[[nodiscard]] static std::expected< CallInput, std::string > forBlob( ipc::Blob blob );

	//! The descriptor to attach to a CALL frame. Borrowed; ownership stays here.
	/** Valid for as long as this object lives, which is at least the call's duration -- a nested call
	 *  reusing this input through INPUT_REF sends the same descriptor again rather than a copy. */
	[[nodiscard]] int fd() const;

	[[nodiscard]] std::size_t size() const { return m_size; }

	//! The mapped bytes, when the input was staged from memory; empty when it is a file on disk.
	/** A file input deliberately has nothing to offer here: not copying it is the point, so the
	 *  server has no mapping of it either. A caller that needs to inspect the content -- the MIME
	 *  scanner -- must either already know the answer from the database or be working from a blob. */
	[[nodiscard]] const ipc::Blob& blob() const { return m_blob; }
};

} // namespace idhan::modules
