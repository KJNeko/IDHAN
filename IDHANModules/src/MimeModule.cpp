#include "MimeModule.hpp"

#include <algorithm>

namespace idhan
{

MimeModuleI::~MimeModuleI() = default;

bool MimeModuleI::canExpandMime( const MimeID mime_id )
{
	return std::ranges::contains( handleableMimes(), mime_id );
}

ModuleType MimeModuleI::type()
{
	return ModuleTypeFlags::MIME_PARSE;
}

} // namespace idhan
