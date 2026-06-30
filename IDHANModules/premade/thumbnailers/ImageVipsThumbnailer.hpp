//
// Created by kj16609 on 6/12/25.
//
#pragma once
#include "ThumbnailerModule.hpp"

class ImageVipsThumbnailer : public idhan::ThumbnailerModuleI
{
  public:

	ImageVipsThumbnailer() = delete;

	ImageVipsThumbnailer( idhan::ModuleCallbacks callbacks ) : ThumbnailerModuleI( callbacks ) {}

	[[nodiscard]] std::vector< std::string_view > handleableMimes() override;

	[[nodiscard]] std::expected< idhan::ThumbnailInfo, idhan::ModuleError > createThumbnail(
		idhan::ModuleCallData& data,
		std::size_t width,
		std::size_t height ) override;

	[[nodiscard]] std::string_view name() override { return "JPG Thumbnailer"; }

	[[nodiscard]] idhan::ModuleVersion version() override { return { .m_major = 1, .m_minor = 0, .m_patch = 0 }; }
};
