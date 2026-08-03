//
// Created by kj16609 on 8/2/26.
//
#pragma once

#include <cstddef>
#include <expected>
#include <memory>
#include <span>
#include <string>

#include "ModuleSink.hpp"
#include "UniqueFd.hpp"

namespace idhan::ipc
{

//! A ModuleSink that writes into an anonymous memory object, which is then handed to the host.
/** The output side of the same idea as Blob: the module writes once, into the memory that will
 *  carry the result across the socket, so a large generated file is never held twice.
 *
 *  Writes go through pwrite rather than a mapping. The copy is identical either way -- module
 *  buffer into page cache -- and pwrite avoids two things a mapping would need: remapping every
 *  time the output outgrows its reservation, and unmapping before sealing (F_SEAL_WRITE fails with
 *  EBUSY while a shared writable mapping is live, which is an easy sequencing bug to introduce and
 *  a confusing one to diagnose). */
class MemfdSink final : public ModuleSink
{
	UniqueFd m_fd {};
	std::size_t m_written { 0 };
	std::size_t m_reserved { 0 };

	MemfdSink() = default;

  public:

	~MemfdSink() override = default;

	MemfdSink( MemfdSink&& ) = delete;
	MemfdSink& operator=( MemfdSink&& ) = delete;

	[[nodiscard]] static std::expected< std::unique_ptr< MemfdSink >, std::string > create();

	[[nodiscard]] std::expected< void, ModuleError > reserve( std::size_t bytes ) override;

	[[nodiscard]] std::expected< void, ModuleError > write( std::span< const std::byte > bytes ) override;

	//! Bytes written so far.
	[[nodiscard]] std::size_t written() const { return m_written; }

	//! Trims to what was actually written, seals, and yields the descriptor.
	/** Trimming matters when a module reserved optimistically and wrote less: without it the result
	 *  would carry the reservation's worth of trailing zeroes as if they were data. Sealing is what
	 *  makes the mapping the host receives genuinely immutable.
	 *
	 *  The sink is spent afterwards and must not be written to again. */
	[[nodiscard]] std::expected< UniqueFd, std::string > finish();
};

} // namespace idhan::ipc
