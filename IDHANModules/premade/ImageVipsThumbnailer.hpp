//
// Created by kj16609 on 6/12/25.
//
#pragma once
#include "ThumbnailerModule.hpp"

class ImageVipsThumbnailer : public ThumbnailerModuleI
{
	std::vector< std::string_view > handleableMimes() override;

	std::expected< ThumbnailInfo, ModuleError > createThumbnail(
		void* data, std::size_t length, std::size_t width, std::size_t height, const std::string mime_name ) override;

	std::string_view name() override { return "JPG Thumbnailer"; }
};
