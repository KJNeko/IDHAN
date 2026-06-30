//
// Created by kj16609 on 11/25/25.
//
#pragma once
#include "ThumbnailerModule.hpp"

class ArchiveThumbnailer : public idhan::ThumbnailerModuleI
{
  public:

	ArchiveThumbnailer() = delete;

	ArchiveThumbnailer( idhan::ModuleCallbacks callbacks ) : ThumbnailerModuleI( callbacks ) {}

	[[nodiscard]] std::string_view name() override { return "Archive generator module"; }

	[[nodiscard]] idhan::ModuleVersion version() override { return { .m_major = 1, .m_minor = 0, .m_patch = 0 }; }

	[[nodiscard]] std::vector< std::string_view > handleableMimes() override;

	[[nodiscard]] std::expected< idhan::ThumbnailInfo, idhan::ModuleError > createThumbnail(
		idhan::ModuleCallData& data,
		std::size_t width,
		std::size_t height ) override;
};
