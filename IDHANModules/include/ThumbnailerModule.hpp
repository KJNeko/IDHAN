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

	[[nodiscard]] virtual std::vector< MimeID > handleableMimes() = 0;

	//! Renders a thumbnail as raw interleaved RGB pixels.
	//! \param width,height Target thumbnail dimensions in pixels.
	[[nodiscard]] virtual std::expected< ThumbnailInfo, ModuleError > createThumbnailRaw(
		ModuleCallData& data,
		std::size_t width,
		std::size_t height ) = 0;

	//! Like createThumbnailRaw, but returns the thumbnail encoded as an in-memory WebP file.
	//! \copydetails createThumbnailRaw
	[[nodiscard]] std::expected< ThumbnailInfo, ModuleError > createThumbnailFile(
		ModuleCallData& data,
		std::size_t width,
		std::size_t height );

	[[nodiscard]] bool canHandle( MimeID mime_id );

	ModuleType type() override;
};
} // namespace idhan