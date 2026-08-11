//
// Created by kj16609 on 8/10/26.
//

#include "EmbeddingModule.hpp"

namespace idhan
{

EmbeddingModuleI::~EmbeddingModuleI() = default;

ModuleType EmbeddingModuleI::type()
{
	return ModuleTypeFlags::EMBEDDING;
}

} // namespace idhan
