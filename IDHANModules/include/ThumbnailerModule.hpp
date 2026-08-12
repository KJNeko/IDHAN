#pragma once

#include <algorithm>
#include <expected>
#include <string_view>
#include <vector>

#include "ModuleBase.hpp"

namespace idhan
{

class FGL_EXPORT ThumbnailerModuleI : public ModuleBase
{
  public:

	ThumbnailerModuleI() = delete;

	ThumbnailerModuleI( ModuleCallbacks callbacks ) : ModuleBase( callbacks ) {}

	~ThumbnailerModuleI() override;

	//! The canonical MIME types this thumbnailer can render.
	[[nodiscard]] virtual std::vector< std::string_view > handleableMimes() = 0;

	//! Renders a thumbnail as raw interleaved RGB pixels.
	//! \param data The source file and its MIME (see ModuleCallData).
	//! \param width,height Target thumbnail dimensions in pixels.
	//! \return The thumbnail, or a ModuleError describing why it could not be produced.
	[[nodiscard]] virtual std::expected< ThumbnailInfo, ModuleError > createThumbnailRaw(
		ModuleCallData& data,
		std::size_t width,
		std::size_t height ) = 0;

	//! Like createThumbnail but returns the thumbnail encoded as an in-memory WEBP file.
	//! \copydetails createThumbnailRaw
	[[nodiscard]] std::expected< ThumbnailInfo, ModuleError > createThumbnailFile(
		ModuleCallData& data,
		std::size_t width,
		std::size_t height );

	//! \return true if \p mime is one of handleableMimes().
	[[nodiscard]] bool canHandle( std::string_view mime );

	//! \return ModuleTypeFlags::THUMBNAILER.
	ModuleType type() override;
};
} // namespace idhan