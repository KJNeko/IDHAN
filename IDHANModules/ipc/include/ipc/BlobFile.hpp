//
// Created by kj16609 on 8/2/26.
//
#pragma once

#include <cstddef>
#include <expected>
#include <span>
#include <utility>

#include "Blob.hpp"
#include "ModuleFile.hpp"

namespace idhan::ipc
{

//! A ModuleFile backed by a mapped memfd.
/** The fallback input path, used where io_uring is unavailable -- notably under Docker's default
 *  seccomp profile, which blocks io_uring_setup outright (see docs/docker.md). It costs a full copy
 *  of the file into anonymous memory, which is what RestrictedRing exists to avoid, but it works
 *  everywhere.
 *
 *  Also the natural handle for bytes that were never a file to begin with: a request body, or
 *  output a module produced that is being passed back through a callback. */
class BlobFile final : public ModuleFile
{
	Blob m_blob;

  public:

	explicit BlobFile( Blob blob ) : m_blob( std::move( blob ) ) {}

	[[nodiscard]] std::size_t size() const override { return m_blob.size(); }

	//! The descriptor backing this handle. Ownership stays here.
	/** Lets the runner forward a handle it already holds as a memfd -- the output of a nested
	 *  generate, on its way into a nested thumbnail -- by attaching the descriptor it already has,
	 *  instead of reading the bytes out and copying them into a second memfd. */
	[[nodiscard]] int fd() const { return m_blob.fd(); }

	[[nodiscard]] std::expected< std::size_t, ModuleError > read( std::span< std::byte > out, std::size_t offset )
		const override;
};

} // namespace idhan::ipc
