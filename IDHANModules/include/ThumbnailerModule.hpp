//
// Created by kj16609 on 6/11/25.
//
#pragma once

#include <vips/vips8>

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

	[[nodiscard]] virtual std::vector< std::string_view > handleableMimes() = 0;

	//! Returns a raw thumbnail in RGB format
	[[nodiscard]] virtual std::expected< ThumbnailInfo, ModuleError > createThumbnail(
		ModuleCallData& data,
		std::size_t width,
		std::size_t height ) = 0;

	//! Returns the thumbnail in a in-memory PNG file
	[[nodiscard]] std::expected< ThumbnailInfo, ModuleError > createThumbnailFile(
		ModuleCallData& data,
		std::size_t width,
		std::size_t height );

	[[nodiscard]] bool canHandle( std::string_view mime );

	ModuleType type() override;
};
} // namespace idhan