//
// Created by kj16609 on 11/12/25.
//
#pragma once
#include "MetadataModule.hpp"
#include "ThumbnailerModule.hpp"

//! Metadata parser for Photoshop PSD files (canvas dimensions, layer count).
class PsdMetadata final : public idhan::MetadataModuleI
{
  public:

	PsdMetadata() = delete;

	PsdMetadata( idhan::ModuleCallbacks callbacks ) : MetadataModuleI( callbacks ) {}

	[[nodiscard]] std::vector< std::string_view > handleableMimes() override;

	[[nodiscard]] std::string_view name() override;

	[[nodiscard]] idhan::ModuleVersion version() override;

	// pure parsing of the input buffer, no shared mutable state: safe to run concurrently
	[[nodiscard]] bool threadSafe() override { return true; }

	[[nodiscard]] std::expected< idhan::MetadataInfo, idhan::ModuleError > parseFile( idhan::ModuleCallData& data )
		override;
};
