//
// Created by kj16609 on 11/13/25.
//
#pragma once

#include "ThumbnailerModule.hpp"

class FFMPEGThumbnailer final : public idhan::ThumbnailerModuleI
{
  public:

	FFMPEGThumbnailer() = delete;

	FFMPEGThumbnailer( idhan::ModuleCallbacks callbacks ) : ThumbnailerModuleI( callbacks ) {}

	[[nodiscard]] std::string_view name() override;

	[[nodiscard]] idhan::ModuleVersion version() override;

	[[nodiscard]] std::vector< std::string_view > handleableMimes() override;

	[[nodiscard]] std::expected< idhan::ThumbnailInfo, idhan::ModuleError > createThumbnail(
		idhan::ModuleCallData& data,
		std::size_t width,
		std::size_t height ) override;
};
