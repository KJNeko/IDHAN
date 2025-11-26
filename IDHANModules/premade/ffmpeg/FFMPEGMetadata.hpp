//
// Created by kj16609 on 11/12/25.
//
#pragma once
#include "MetadataModule.hpp"

class FFMPEGMetadata final : public idhan::MetadataModuleI
{
  public:

	FFMPEGMetadata( idhan::ModuleCallbacks callbacks ) : MetadataModuleI( callbacks ) {}

	std::string_view name() override;

	idhan::ModuleVersion version() override;

	std::vector< std::string_view > handleableMimes() override;

	std::expected< idhan::MetadataInfo, idhan::ModuleError > parseFile( idhan::ModuleCallData& data ) override;
};
