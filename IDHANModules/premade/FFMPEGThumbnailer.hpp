//
// Created by kj16609 on 11/13/25.
//
#pragma once

#include "ThumbnailerModule.hpp"

class FFMPEGThumbnailer final : public idhan::ThumbnailerModuleI
{
  public:

	std::string_view name() override;

	idhan::ModuleVersion version() override;

	std::vector< std::string_view > handleableMimes() override;

	std::expected< ThumbnailInfo, idhan::ModuleError > createThumbnail(
		const void* data,
		std::size_t length,
		std::size_t width,
		std::size_t height,
		std::string mime_name ) override;
};
