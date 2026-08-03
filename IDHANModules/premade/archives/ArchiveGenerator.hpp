//
// Created by kj16609 on 11/25/25.
//
#pragma once
#include "GeneratorModule.hpp"

//! Generator that extracts a specific member file from an archive, addressed by its SHA-256 hash.
class ArchiveGenerator : public idhan::GeneratorModuleI
{
  public:

	ArchiveGenerator() = delete;

	ArchiveGenerator( idhan::ModuleCallbacks callbacks ) : GeneratorModuleI( callbacks ) {}

	[[nodiscard]] std::string_view name() override { return "Archive generator module"; }

	[[nodiscard]] idhan::ModuleVersion version() override { return { .m_major = 1, .m_minor = 0, .m_patch = 0 }; }

	// each call opens its own libarchive handle, no shared state: safe to run concurrently
	[[nodiscard]] bool threadSafe() override { return true; }

	[[nodiscard]] std::vector< std::string_view > handleableMimes() override;

	[[nodiscard]] std::expected< void, idhan::ModuleError > generate(
		idhan::ModuleCallData& data,
		std::array< std::byte, 256 / 8 > desired_hash,
		idhan::ModuleSink& out ) override;
};
