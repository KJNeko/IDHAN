//
// Created by kj16609 on 6/11/25.
//
#pragma once
#include <array>
#include <expected>
#include <string>
#include <variant>
#include <vector>

#include "IDHANTypes.hpp"
#include "MetadataInfo.hpp"
#include "ModuleBase.hpp"

namespace idhan
{

class FGL_EXPORT MetadataModuleI : public ModuleBase
{
  public:

	MetadataModuleI() = delete;

	MetadataModuleI( ModuleCallbacks callbacks ) : ModuleBase( callbacks ) {}

	~MetadataModuleI() override;

	[[nodiscard]] virtual std::vector< std::string_view > handleableMimes() = 0;

	[[nodiscard]] virtual std::expected< MetadataInfo, ModuleError > parseFile( ModuleCallData& data ) = 0;

	[[nodiscard]] bool canHandle( std::string_view mime );

	ModuleType type() override;
};
} // namespace idhan