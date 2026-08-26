#pragma once
#include "MimeModule.hpp"

//! Specializes a zip container into a specific archive mime id.
class ArchiveMimeParser : public idhan::MimeModuleI
{
  public:

	ArchiveMimeParser() = delete;

	ArchiveMimeParser( idhan::ModuleCallbacks callbacks ) : MimeModuleI( callbacks ) {}

	[[nodiscard]] std::string_view name() override;

	[[nodiscard]] idhan::ModuleVersion version() override;

	// each call opens its own libarchive handle, no shared state: safe to run concurrently
	[[nodiscard]] bool threadSafe() override { return true; }

	[[nodiscard]] std::vector< idhan::MimeID > handleableMimes() override;

	[[nodiscard]] std::expected< idhan::MimeID, idhan::ModuleError > parseMime( idhan::ModuleCallData& data ) override;
};
