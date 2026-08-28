#include "MetadataModule.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>

namespace idhan
{

MetadataModuleI::~MetadataModuleI() = default;

bool MetadataModuleI::canHandle( const MimeID mime_id )
{
	return std::ranges::contains( handleableMimes(), mime_id );
}

ModuleType MetadataModuleI::type()
{
	return ModuleTypeFlags::METADATA;
}
} // namespace idhan
