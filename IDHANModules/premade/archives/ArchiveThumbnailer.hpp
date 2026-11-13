#pragma once
#include "ThumbnailerModule.hpp"

//! Thumbnailer for archives: composites thumbnails of contained files into a grid. Re-entrant: it
//! asks the host to thumbnail members, and the host's callback depth limit bounds the recursion.
class ArchiveThumbnailer : public idhan::ThumbnailerModuleI
{
  public:

	ArchiveThumbnailer() = delete;

	ArchiveThumbnailer( idhan::ModuleCallbacks callbacks ) : ThumbnailerModuleI( callbacks ) {}

	[[nodiscard]] std::string_view name() override { return "Archive thumbnailer module"; }

	[[nodiscard]] idhan::ModuleVersion version() override { return { .m_major = 1, .m_minor = 0, .m_patch = 0 }; }

	[[nodiscard]] bool threadSafe() override { return true; }

	//! The member grid is composited and encoded through vips
	[[nodiscard]] bool singleThreaded() override { return false; }

	[[nodiscard]] std::vector< idhan::MimeID > handleableMimes() override;

	[[nodiscard]] std::expected< idhan::ThumbnailInfo, idhan::ModuleError > createThumbnailRaw(
		idhan::ModuleCallData& data,
		std::size_t width,
		std::size_t height ) override;
};
