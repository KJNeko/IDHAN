#pragma once

#include <cstddef>
#include <expected>
#include <span>

#include "ModuleCommon.hpp"

namespace idhan
{

class FGL_EXPORT ModuleSink
{
  public:

	virtual ~ModuleSink() = default;

	ModuleSink() = default;

	ModuleSink( const ModuleSink& ) = delete;
	ModuleSink& operator=( const ModuleSink& ) = delete;
	ModuleSink( ModuleSink&& ) = delete;
	ModuleSink& operator=( ModuleSink&& ) = delete;

	[[nodiscard]] virtual std::expected< void, ModuleError > reserve( std::size_t bytes ) = 0;

	[[nodiscard]] virtual std::expected< void, ModuleError > write( std::span< const std::byte > bytes ) = 0;
};

} // namespace idhan
