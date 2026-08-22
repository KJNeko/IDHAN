#pragma once
#include "ThumbnailerModule.hpp"

//! Thumbnailer for raster image formats handled by libvips (JPEG, PNG, WebP, TIFF, ...).
class ImageVipsThumbnailer : public idhan::ThumbnailerModuleI
{
  public:

	ImageVipsThumbnailer() = delete;

	ImageVipsThumbnailer( idhan::ModuleCallbacks callbacks ) : ThumbnailerModuleI( callbacks ) {}

	[[nodiscard]] std::vector< idhan::MimeID > handleableMimes() override;

	[[nodiscard]] std::expected< idhan::ThumbnailInfo, idhan::ModuleError > createThumbnailRaw(
		idhan::ModuleCallData& data,
		std::size_t width,
		std::size_t height ) override;

	[[nodiscard]] std::string_view name() override { return "JPG Thumbnailer"; }

	[[nodiscard]] idhan::ModuleVersion version() override { return { .m_major = 1, .m_minor = 0, .m_patch = 0 }; }

	// no shared mutable state: each call builds its own vips image, safe to run concurrently
	[[nodiscard]] bool threadSafe() override { return true; }

	// VIPS_INIT builds the operation and type registries; far too expensive to pay per call
	[[nodiscard]] idhan::ModuleResidency residency() override { return idhan::ModuleResidency::PERSISTENT; }
};
