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

	[[nodiscard]] std::expected< UniqueFd, std::string > seal();
};

} // namespace idhan::ipc
