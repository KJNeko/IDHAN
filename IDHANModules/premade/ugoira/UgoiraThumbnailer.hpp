#pragma once
#include "ThumbnailerModule.hpp"

//! Renders a Pixiv Ugoira as an animated WebP. Frames come out of the zip through the host's
//! generate callback, so this module never opens an archive itself.
class UgoiraThumbnailer : public idhan::ThumbnailerModuleI
{
  public:

	UgoiraThumbnailer() = delete;

	UgoiraThumbnailer( idhan::ModuleCallbacks callbacks ) : ThumbnailerModuleI( callbacks ) {}

	[[nodiscard]] std::string_view name() override { return "Ugoira thumbnailer module"; }

	[[nodiscard]] idhan::ModuleVersion version() override { return { .m_major = 1, .m_minor = 0, .m_patch = 0 }; }

	// no shared state; every call owns its own vips objects
	[[nodiscard]] bool threadSafe() override { return true; }

	[[nodiscard]] idhan::ModuleResidency residency() override { return idhan::ModuleResidency::PERSISTENT; }

	[[nodiscard]] std::size_t rssCeilingMb() override { return 512; }

	[[nodiscard]] std::vector< idhan::MimeID > handleableMimes() override;

	[[nodiscard]] std::expected< idhan::ThumbnailInfo, idhan::ModuleError > createThumbnailRaw(
		idhan::ModuleCallData& data,
		std::size_t width,
		std::size_t height ) override;
};
