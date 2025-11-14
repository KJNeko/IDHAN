//
// Created by kj16609 on 6/12/25.
//
#pragma once
#include "ThumbnailerModule.hpp"

class ImageVipsThumbnailer : public idhan::ThumbnailerModuleI
{
	std::vector< std::string_view > handleableMimes() override;

	std::expected< ThumbnailInfo, idhan::ModuleError > createThumbnail(
		const void* data,
		std::size_t length,
		std::size_t width,
		std::size_t height,
		std::string mime_name ) override;

	std::string_view name() override { return "JPG Thumbnailer"; }

	idhan::ModuleVersion version() override { return { .m_major = 1, .m_minor = 0, .m_patch = 0 }; }
};
