#pragma once

#include <cstddef>
#include <expected>
#include <memory>
#include <span>

#include "ModuleCommon.hpp"

namespace idhan
{

//! Read-only random access to the file a module call is about.
/** Replaces the flat data_view modules used to receive. The difference that matters is not the
 *  shape but the ownership: a view meant the host had already materialised the whole file
 *  somewhere, whether or not the module wanted it. A 4 GiB video was pulled entirely into memory so
 *  a thumbnailer could decode one frame near the start.
 *
 *  There is deliberately no readAll(). A module's peak memory should be what that module chose to
 *  allocate, visible at the site that chose it -- a convenience that quietly materialises the whole
 *  file is exactly the thing this interface exists to remove. A module that genuinely needs the
 *  file contiguous (the PSD parser walks a raw pointer) allocates it itself, in its own code, where
 *  the cost can be seen in review.
 *
 *  The implementation a module receives depends on how the host could deliver the bytes: normally
 *  reads go through a per-call io_uring restricted to this one file, and fall back to a mapped
 *  memfd where io_uring is unavailable. Modules never see which, and must not care. */
class FGL_EXPORT ModuleFile
{
  public:

	virtual ~ModuleFile() = default;

	ModuleFile() = default;

	ModuleFile( const ModuleFile& ) = delete;
	ModuleFile& operator=( const ModuleFile& ) = delete;
	ModuleFile( ModuleFile&& ) = delete;
	ModuleFile& operator=( ModuleFile&& ) = delete;

	//! Total size of the file in bytes. Known without reading anything.
	[[nodiscard]] virtual std::size_t size() const = 0;

	//! Reads into \p out, starting at \p offset in the file.
	/** \return How many bytes were written to \p out. Short only at end of file, so a return less
	 *          than out.size() means the file ended, never that the caller should retry.
	 *
	 *  Every byte of storage involved belongs to the caller. Reading past the end yields 0 rather
	 *  than an error: a demuxer probing beyond the end is normal, not a failure. */
	[[nodiscard]] virtual std::expected< std::size_t, ModuleError > read(
		std::span< std::byte > out,
		std::size_t offset ) const = 0;

	//! Wraps caller-owned memory as a ModuleFile.
	/** For bytes a module produced itself and wants to hand back through a callback -- an archive
	 *  thumbnailer passing an extracted member to be thumbnailed, say. Wraps; it does not copy, so
	 *  \p bytes must outlive the returned handle. */
	[[nodiscard]] static std::unique_ptr< ModuleFile > fromBytes( std::span< const std::byte > bytes );
};

} // namespace idhan
