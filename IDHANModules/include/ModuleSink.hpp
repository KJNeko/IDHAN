//
// Created by kj16609 on 8/2/26.
//
#pragma once

#include <cstddef>
#include <expected>
#include <span>

#include "ModuleCommon.hpp"

namespace idhan
{

//! Write-only sequential output for a module that produces a derived file.
/** The counterpart to ModuleFile, and it exists for the same reason. A generator used to return
 *  std::vector<std::byte>, so extracting a 500 MB archive member materialised it in the worker's
 *  heap and then copied it again into the shared memory that carries it back. Two copies of
 *  something inherently large.
 *
 *  Generator output *is* large -- that is not a problem to be designed away, and this interface
 *  does not try to. It ensures the output exists once: the module writes straight into the memory
 *  the host will hand onwards, so the only remaining copy is the unavoidable one out of whatever
 *  buffer the decoder filled. */
class FGL_EXPORT ModuleSink
{
  public:

	virtual ~ModuleSink() = default;

	ModuleSink() = default;

	ModuleSink( const ModuleSink& ) = delete;
	ModuleSink& operator=( const ModuleSink& ) = delete;
	ModuleSink( ModuleSink&& ) = delete;
	ModuleSink& operator=( ModuleSink&& ) = delete;

	//! Declares the final size up front, when the module knows it.
	/** Sizing the destination once turns a 500 MB write into a single allocation instead of a
	 *  series of grow-and-remap steps, so call it whenever the size is available -- libarchive
	 *  reports an entry's uncompressed size from its header in the common case.
	 *
	 *  Optional. A sink with no reservation grows as it is written, which is correct but is the
	 *  slow path. Calling it more than once, or reserving less than is ultimately written, is not
	 *  an error; the reservation is a hint about the expected total, not a limit. */
	[[nodiscard]] virtual std::expected< void, ModuleError > reserve( std::size_t bytes ) = 0;

	//! Appends \p bytes to the output.
	/** The sink owns the destination and the module keeps no copy. Bytes are consumed by the time
	 *  this returns, so the caller may reuse its buffer immediately. */
	[[nodiscard]] virtual std::expected< void, ModuleError > write( std::span< const std::byte > bytes ) = 0;
};

} // namespace idhan
