#pragma once

#include "ThumbnailerModule.hpp"

//! Thumbnailer for video via FFmpeg, decoding a representative frame to RGB.
class FFMPEGThumbnailer final : public idhan::ThumbnailerModuleI
{
  public:

	FFMPEGThumbnailer() = delete;

	FFMPEGThumbnailer( idhan::ModuleCallbacks callbacks ) : ThumbnailerModuleI( callbacks ) {}

	[[nodiscard]] std::string_view name() override;

	[[nodiscard]] idhan::ModuleVersion version() override;

	// each call owns its AVFormatContext/codec contexts, no shared state: safe to run concurrently
	[[nodiscard]] bool threadSafe() override { return true; }

	// createThumbnailFile encodes through vips, so this worker pays VIPS_INIT as well as codec setup
	[[nodiscard]] idhan::ModuleResidency residency() override { return idhan::ModuleResidency::PERSISTENT; }

	[[nodiscard]] std::vector< idhan::MimeID > handleableMimes() override;

	[[nodiscard]] std::expected< idhan::ThumbnailInfo, idhan::ModuleError > createThumbnailRaw(
		idhan::ModuleCallData& data,
		std::size_t width,
		std::size_t height ) override;
};
