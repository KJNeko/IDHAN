//
// Created by kj16609 on 11/13/25.
//
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

	[[nodiscard]] std::vector< std::string_view > handleableMimes() override;

	[[nodiscard]] std::expected< idhan::ThumbnailInfo, idhan::ModuleError > createThumbnailRaw(
		idhan::ModuleCallData& data,
		std::size_t width,
		std::size_t height ) override;
};
