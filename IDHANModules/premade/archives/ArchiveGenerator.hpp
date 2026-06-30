//
// Created by kj16609 on 11/25/25.
//
#pragma once
#include "GeneratorModule.hpp"

class ArchiveGenerator : public idhan::GeneratorModuleI
{
  public:

	ArchiveGenerator() = delete;

	ArchiveGenerator( idhan::ModuleCallbacks callbacks ) : GeneratorModuleI( callbacks ) {}

	[[nodiscard]] std::string_view name() override { return "Archive generator module"; }

	[[nodiscard]] idhan::ModuleVersion version() override { return { .m_major = 1, .m_minor = 0, .m_patch = 0 }; }

	[[nodiscard]] std::vector< std::string_view > handleableMimes() override;

	[[nodiscard]] std::expected< std::vector< std::byte >, idhan::ModuleError > generate(
		idhan::ModuleCallData& data,
		std::array< std::byte, 256 / 8 > desired_hash ) override;
};
