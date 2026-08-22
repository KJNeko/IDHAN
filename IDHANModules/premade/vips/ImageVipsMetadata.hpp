#pragma once
#include <expected>
#include <string_view>
#include <vector>

#include "MetadataModule.hpp"
#include "ThumbnailerModule.hpp"

//! Metadata parser for raster image formats handled by libvips (JPEG, PNG, WebP, TIFF, ...).
class ImageVipsMetadata final : public idhan::MetadataModuleI
{
  public:

	ImageVipsMetadata( idhan::ModuleCallbacks callbacks ) : MetadataModuleI( callbacks ) {}

	ImageVipsMetadata() = delete;

	[[nodiscard]] std::vector< idhan::MimeID > handleableMimes() override;
	[[nodiscard]] std::expected< idhan::MetadataInfo, idhan::ModuleError > parseFile( idhan::ModuleCallData& data )
		override;

	[[nodiscard]] std::string_view name() override { return "JPG Metadata Parser"; }

	[[nodiscard]] idhan::ModuleVersion version() override { return { .m_major = 1, .m_minor = 0, .m_patch = 0 }; }

	// no shared mutable state: each call builds its own vips image, safe to run concurrently
	[[nodiscard]] bool threadSafe() override { return true; }

	// VIPS_INIT builds the operation and type registries; far too expensive to pay per call
	[[nodiscard]] idhan::ModuleResidency residency() override { return idhan::ModuleResidency::PERSISTENT; }
};
