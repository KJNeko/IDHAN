#pragma once
#include "GeneratorModule.hpp"
#include "MetadataModule.hpp"
#include "ThumbnailerModule.hpp"

//! Metadata parser for archives via libarchive (contained file hashes, decompressed size, encryption).
class ArchiveMetadata : public idhan::MetadataModuleI
{
  public:

	ArchiveMetadata() = delete;

	ArchiveMetadata( idhan::ModuleCallbacks callbacks ) : MetadataModuleI( callbacks ) {}

	[[nodiscard]] std::string_view name() override;

	[[nodiscard]] idhan::ModuleVersion version() override;

	// each call opens its own libarchive handle, no shared state: safe to run concurrently
	[[nodiscard]] bool threadSafe() override { return true; }

	[[nodiscard]] std::vector< idhan::MimeID > handleableMimes() override;

	[[nodiscard]] std::expected< idhan::MetadataInfo, idhan::ModuleError > parseFile( idhan::ModuleCallData& data )
		override;
};
