#pragma once

#include <cstddef>
#include <expected>
#include <memory>
#include <span>

#include "ModuleCommon.hpp"

namespace idhan
{

class FGL_EXPORT ModuleFile
{
  public:

	virtual ~ModuleFile() = default;

	ModuleFile() = default;

	ModuleFile( const ModuleFile& ) = delete;
	ModuleFile& operator=( const ModuleFile& ) = delete;
	ModuleFile( ModuleFile&& ) = delete;
	ModuleFile& operator=( ModuleFile&& ) = delete;

	[[nodiscard]] virtual std::size_t size() const = 0;

	[[nodiscard]] virtual std::expected< std::size_t, ModuleError > read(
		std::span< std::byte > out,
		std::size_t offset ) const = 0;

	[[nodiscard]] virtual std::span< const std::byte > mapped() const { return {}; }

	[[nodiscard]] static std::unique_ptr< ModuleFile > fromBytes( std::span< const std::byte > bytes );
};

} // namespace idhan
