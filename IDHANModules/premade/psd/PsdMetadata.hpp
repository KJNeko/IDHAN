#pragma once
#include "MetadataModule.hpp"
#include "ThumbnailerModule.hpp"

//! Metadata parser for Photoshop PSD files (canvas dimensions, layer count).
class PsdMetadata final : public idhan::MetadataModuleI
{
  public:

	PsdMetadata() = delete;

	PsdMetadata( idhan::ModuleCallbacks callbacks ) : MetadataModuleI( callbacks ) {}

	[[nodiscard]] std::vector< idhan::MimeID > handleableMimes() override;

	[[nodiscard]] std::string_view name() override;

	[[nodiscard]] idhan::ModuleVersion version() override;

	// pure parsing of the input buffer, no shared mutable state: safe to run concurrently
	[[nodiscard]] bool threadSafe() override { return true; }

	// shares a worker with PsdThumbnailer, which composites through vips
	[[nodiscard]] idhan::ModuleResidency residency() override { return idhan::ModuleResidency::PERSISTENT; }

	[[nodiscard]] std::expected< idhan::MetadataInfo, idhan::ModuleError > parseFile( idhan::ModuleCallData& data )
		override;
};
