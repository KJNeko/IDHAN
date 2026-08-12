#include "EmbeddingModule.hpp"

namespace idhan
{

EmbeddingModuleI::~EmbeddingModuleI() = default;

ModuleType EmbeddingModuleI::type()
{
	return ModuleTypeFlags::EMBEDDING;
}

} // namespace idhan
