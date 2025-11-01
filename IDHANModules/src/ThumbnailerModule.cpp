//
// Created by kj16609 on 6/11/25.
//

#include "ThumbnailerModule.hpp"

namespace idhan
{
ThumbnailerModuleI::ThumbnailerModuleI() = default;

ThumbnailerModuleI::~ThumbnailerModuleI() = default;

bool ThumbnailerModuleI::canHandle( const std::string_view mime )
{
	return std::ranges::any_of(
		handleableMimes(),
		[ &mime ]( const std::string_view handleable_mime ) noexcept -> bool { return mime == handleable_mime; } );
}

ModuleType ThumbnailerModuleI::type()
{
	return ModuleTypeFlags::THUMBNAILER;
}
} // namespace idhan