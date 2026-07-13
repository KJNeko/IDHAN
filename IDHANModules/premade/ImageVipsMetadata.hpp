//
// Created by kj16609 on 6/11/25.
//
#pragma once
#include <expected>
#include <string_view>
#include <vector>

#include "MetadataModule.hpp"
#include "ThumbnailerModule.hpp"

class ImageVipsMetadata final : public idhan::MetadataModuleI
{
  public:

	ImageVipsMetadata( idhan::ModuleCallbacks callbacks ) : MetadataModuleI( callbacks ) {}

	ImageVipsMetadata() = delete;

	[[nodiscard]] std::vector< std::string_view > handleableMimes() override;
	[[nodiscard]] std::expected< idhan::MetadataInfo, idhan::ModuleError > parseFile( idhan::ModuleCallData& data ) override;

	[[nodiscard]] std::string_view name() override { return "JPG Metadata Parser"; }

	[[nodiscard]] idhan::ModuleVersion version() override { return { .m_major = 1, .m_minor = 0, .m_patch = 0 }; }

	// no shared mutable state: each call builds its own vips image, safe to run concurrently
	[[nodiscard]] bool threadSafe() override { return true; }
};
