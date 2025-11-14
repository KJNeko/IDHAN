//
// Created by kj16609 on 6/11/25.
//
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

	struct ThumbnailInfo
	{
		std::vector< std::byte > data {};
		std::size_t width, height;
	};

	ThumbnailerModuleI();

	~ThumbnailerModuleI() override;

	virtual std::vector< std::string_view > handleableMimes() = 0;

	virtual std::expected< ThumbnailInfo, ModuleError > createThumbnail(
		const void* data,
		std::size_t length,
		std::size_t width,
		std::size_t height,
		std::string mime_name ) = 0;

	bool canHandle( std::string_view mime );

	ModuleType type() override;
};
} // namespace idhan