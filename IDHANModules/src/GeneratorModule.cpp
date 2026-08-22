#include "GeneratorModule.hpp"

#include <algorithm>

namespace idhan
{

GeneratorModuleI::~GeneratorModuleI() = default;

bool GeneratorModuleI::canHandle( const MimeID mime_id )
{
	return std::ranges::contains( handleableMimes(), mime_id );
}

ModuleType GeneratorModuleI::type()
{
	return ModuleTypeFlags::GENERATOR;
}

} // namespace idhan