#pragma once

#include "ThumbnailerModule.hpp"

class FFMPEGThumbnailer final : public idhan::ThumbnailerModuleI
{
  public:

	FFMPEGThumbnailer() = delete;

	FFMPEGThumbnailer( idhan::ModuleCallbacks callbacks ) : ThumbnailerModuleI( callbacks ) {}

	[[nodiscard]] std::string_view name() override;

	[[nodiscard]] idhan::ModuleVersion version() override;

	//! Each call owns its AVFormatContext/codec contexts, no shared state: safe to run concurrently
	[[nodiscard]] bool threadSafe() override { return true; }

	//! The decoder runs its own thread pool, sized from the render budget
	[[nodiscard]] bool singleThreaded() override { return false; }

	//! createThumbnailFile encodes through vips, so this worker pays VIPS_INIT as well as codec setup
	[[nodiscard]] idhan::ModuleResidency residency() override { return idhan::ModuleResidency::PERSISTENT; }

	[[nodiscard]] std::vector< idhan::MimeID > handleableMimes() override;

	//! Returns an RGB video preview near 10% playback, fitted within the requested dimensions.
	[[nodiscard]] std::expected< idhan::ThumbnailInfo, idhan::ModuleError > createThumbnailRaw(
		idhan::ModuleCallData& data,
		std::size_t width,
		std::size_t height ) override;
};
