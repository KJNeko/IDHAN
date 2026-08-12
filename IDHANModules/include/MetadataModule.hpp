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

	//! The canonical MIME types this parser can extract metadata from.
	[[nodiscard]] virtual std::vector< std::string_view > handleableMimes() = 0;

	//! Extracts metadata (dimensions, duration, contained files, etc.) from a file.
	//! \param data The source file and its MIME (see ModuleCallData).
	//! \return A populated MetadataInfo, or a ModuleError if the file could not be parsed.
	[[nodiscard]] virtual std::expected< MetadataInfo, ModuleError > parseFile( ModuleCallData& data ) = 0;

	//! \return true if \p mime is one of handleableMimes().
	[[nodiscard]] bool canHandle( std::string_view mime );

	//! \return ModuleTypeFlags::METADATA.
	ModuleType type() override;
};
} // namespace idhan