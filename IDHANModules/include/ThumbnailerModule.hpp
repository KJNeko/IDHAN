//
// Created by kj16609 on 6/11/25.
//
#pragma once
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
	virtual ~ThumbnailerModuleI();

	virtual std::vector< std::string_view > handleableMimes() = 0;

	bool canHandle( const std::string_view mime )
	{
		for ( auto& handleable : handleableMimes() )
		{
			if ( handleable == mime ) return true;
		}

		return false;
	}

	virtual std::expected< ThumbnailInfo, ModuleError > createThumbnail(
		void* data, std::size_t length, std::size_t width, std::size_t height, const std::string mime_name ) = 0;

	ModuleType type() override { return ModuleTypeFlags::THUMBNAILER; }
};
} // namespace idhan