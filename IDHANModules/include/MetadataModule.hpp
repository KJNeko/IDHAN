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

	[[nodiscard]] virtual std::vector< MimeID > handleableMimes() = 0;

	//! Opt in only for MIME types whose image metadata can contain a pHash.
	//! An empty result keeps metadata-only parsers out of hash backfills.
	[[nodiscard]] virtual std::vector< MimeID > phashMimes() { return {}; }

	//! Extracts metadata (dimensions, duration, contained files, etc.) from a file.
	[[nodiscard]] virtual std::expected< MetadataInfo, ModuleError > parseFile( ModuleCallData& data ) = 0;

	[[nodiscard]] bool canHandle( MimeID mime_id );

	ModuleType type() override;
};
} // namespace idhan