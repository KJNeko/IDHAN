//
// Created by kj16609 on 11/24/25.
//
#pragma once
#include "GeneratorModule.hpp"
#include "MetadataModule.hpp"
#include "ThumbnailerModule.hpp"

class ArchiveMetadata : public idhan::MetadataModuleI
{
  public:

	ArchiveMetadata() = delete;

	ArchiveMetadata( idhan::ModuleCallbacks callbacks ) : MetadataModuleI( callbacks ) {}

	std::string_view name() override;

	idhan::ModuleVersion version() override;

	std::vector< std::string_view > handleableMimes() override;

	std::expected< idhan::MetadataInfo, idhan::ModuleError > parseFile( idhan::ModuleCallData& data ) override;
};
