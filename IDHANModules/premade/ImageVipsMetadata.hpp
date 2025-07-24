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

	ImageVipsMetadata() = default;

	std::vector< std::string_view > handleableMimes() override;
	std::expected< idhan::MetadataInfo, idhan::ModuleError >
		parseFile( void* data, std::size_t length, std::string mime_name ) override;

	std::string_view name() override { return "JPG Metadata Parser"; }
};
