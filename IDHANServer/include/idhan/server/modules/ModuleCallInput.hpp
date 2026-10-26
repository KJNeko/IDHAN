#pragma once

#include <folly/File.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include "idhan/ipc/binary/Messages.hpp"

namespace idhan::server::modules
{

// Owns the descriptor lifetime for one module call. The descriptor is moved
// into the frame only after the call has been admitted by a worker.
class ModuleCallInput
{
	std::shared_ptr< const void > m_owner;
	std::optional< folly::File > m_file;
	std::uint64_t m_offset { 0 };
	std::uint64_t m_size { 0 };
	std::optional< std::filesystem::path > m_filename;

  public:

	ModuleCallInput(
		std::shared_ptr< const void >,
		folly::File,
		std::uint64_t offset,
		std::uint64_t size,
		std::optional< std::filesystem::path > filename );
	~ModuleCallInput();
	ModuleCallInput( ModuleCallInput&& ) noexcept;
	ModuleCallInput& operator=( ModuleCallInput&& ) noexcept;
	ModuleCallInput( const ModuleCallInput& ) = delete;
	ModuleCallInput& operator=( const ModuleCallInput& ) = delete;

	[[nodiscard]] ipc::binary::Input wireInput( std::uint32_t descriptor_index ) const;
	[[nodiscard]] std::vector< folly::File > takeDescriptors() &&;
	[[nodiscard]] std::uint64_t offset() const noexcept;
	[[nodiscard]] std::uint64_t size() const noexcept;
};

} // namespace idhan::server::modules
