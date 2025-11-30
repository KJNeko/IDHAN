//
// Created by kj16609 on 11/25/25.
//
#pragma once
#include "ThumbnailerModule.hpp"

class PsdThumbnailer final : public idhan::ThumbnailerModuleI
{
  public:

	PsdThumbnailer() = delete;

	PsdThumbnailer( idhan::ModuleCallbacks callbacks ) : ThumbnailerModuleI( callbacks ) {}

	std::vector< std::string_view > handleableMimes() override;

	std::string_view name() override;

	idhan::ModuleVersion version() override;

	std::expected< idhan::ThumbnailInfo, idhan::ModuleError > createThumbnail(
		idhan::ModuleCallData& data,
		std::size_t width,
		std::size_t height );
};
