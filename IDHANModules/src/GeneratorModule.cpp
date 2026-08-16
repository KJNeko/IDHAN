#include "GeneratorModule.hpp"

#include <algorithm>

namespace idhan
{

GeneratorModuleI::~GeneratorModuleI() = default;

bool GeneratorModuleI::canHandle( const std::string_view mime )
{
	return std::ranges::any_of(
		handleableMimes(),
		[ &mime ]( const std::string_view handleable_mime ) noexcept -> bool { return mime == handleable_mime; } );
}

ModuleType GeneratorModuleI::type()
{
	return ModuleTypeFlags::GENERATOR;
}

} // namespace idhan