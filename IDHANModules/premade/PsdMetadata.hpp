//
// Created by kj16609 on 11/12/25.
//
#pragma once
#include "MetadataModule.hpp"
#include "ThumbnailerModule.hpp"

class PsdMetadata final : public idhan::MetadataModuleI
{
  public:

	PsdMetadata() = default;

	std::vector< std::string_view > handleableMimes() override;

	std::string_view name() override;

	idhan::ModuleVersion version() override;

	std::expected< idhan::MetadataInfo, idhan::ModuleError > parseFile(
		const void* data,
		std::size_t length,
		std::string mime_name ) override;
};

class PsdThumbnailer final : public idhan::ThumbnailerModuleI
{
  public:

	PsdThumbnailer() = default;

	std::vector< std::string_view > handleableMimes() override;

	std::string_view name() override;

	idhan::ModuleVersion version() override;

	std::expected< ThumbnailInfo, idhan::ModuleError > createThumbnail(
		const void* data,
		std::size_t length,
		std::size_t target_width,
		std::size_t target_height,
		std::string mime_name ) override;
};