//
// Created by kj16609 on 11/1/25.
//

#include "MetadataModule.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>

namespace idhan
{

MetadataModuleI::~MetadataModuleI() = default;

bool MetadataModuleI::canHandle( const std::string_view mime )
{
	const auto handleable_mimes { handleableMimes() };
	return std::ranges::any_of(
		handleable_mimes,
		[ &mime ]( const std::string_view handleable_mime ) noexcept -> bool { return mime == handleable_mime; } );
}

ModuleType MetadataModuleI::type()
{
	return ModuleTypeFlags::METADATA;
}
} // namespace idhan
