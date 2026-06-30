//
// Created by kj16609 on 11/24/25.
//
#pragma once
#include <json/value.h>

#include <expected>

#include "IDHANTypes.hpp"
#include "ModuleBase.hpp"

namespace idhan
{

class FGL_EXPORT GeneratorModuleI : public ModuleBase
{
  public:

	GeneratorModuleI() = delete;

	GeneratorModuleI( ModuleCallbacks callbacks ) : ModuleBase( callbacks ) {}

	~GeneratorModuleI() override;

	[[nodiscard]] virtual std::vector< std::string_view > handleableMimes() = 0;

	[[nodiscard]] virtual std::expected< std::vector< std::byte >, idhan::ModuleError > generate(
		ModuleCallData& data,
		std::array< std::byte, 256 / 8 > desired_hash ) = 0;

	[[nodiscard]] bool canHandle( std::string_view mime );

	ModuleType type() override;
};

} // namespace idhan