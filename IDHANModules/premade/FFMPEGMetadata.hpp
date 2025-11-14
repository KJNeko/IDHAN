//
// Created by kj16609 on 11/12/25.
//
#pragma once
#include "MetadataModule.hpp"


class FFMPEGMetadata final : public idhan::MetadataModuleI
{
  public:

	std::string_view name() override;

	idhan::ModuleVersion version() override;

	std::vector< std::string_view > handleableMimes() override;

	std::expected< idhan::MetadataInfo, idhan::ModuleError > parseFile(
		const void* data,
		std::size_t length,
		std::string mime_name ) override;
};
