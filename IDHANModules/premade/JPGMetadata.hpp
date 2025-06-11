//
// Created by kj16609 on 6/11/25.
//
#pragma once
#include <cstddef>
#include <expected>
#include <string_view>
#include <vector>

#include "MetadataModule.hpp"
#include "ThumbnailerModule.hpp"

class JPGMetadata final : public MetadataModuleI
{
  public:

	JPGMetadata() = default;

	std::vector< std::string_view > handleableMimes() override;
	std::expected< MetadataInfo, ModuleError > parseImage( void* data, std::size_t length ) override;

	std::string_view name() override { return "JPG Metadata Parser"; }
};

class JPGThumbnailer : public ThumbnailerModuleI
{
	std::vector< std::string_view > handleableMimes() override;

	std::expected< ThumbnailInfo, ModuleError > createThumbnail(
		void* data, std::size_t length, std::size_t width, std::size_t height, const std::string mime_name ) override;

	std::string_view name() override { return "JPG Thumbnailer"; }
};
