//
// Created by kj16609 on 11/12/25.
//
#pragma once
#include "MetadataModule.hpp"
#include "ThumbnailerModule.hpp"

class PsdMetadata final : public idhan::MetadataModuleI
{
  public:

	PsdMetadata() = delete;

	PsdMetadata( idhan::ModuleCallbacks callbacks ) : MetadataModuleI( callbacks ) {}

	std::vector< std::string_view > handleableMimes() override;

	std::string_view name() override;

	idhan::ModuleVersion version() override;

	std::expected< idhan::MetadataInfo, idhan::ModuleError > parseFile( idhan::ModuleCallData& data ) override;
};
