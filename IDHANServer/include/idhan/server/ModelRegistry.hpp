#pragma once

#include <memory>
#include <string>
#include <vector>

#include "idhan/runtime/Task.hpp"

namespace idhan::server
{
struct ModelInfo
{
	std::string name;
	std::string version;
};

class ModelRegistry
{
	struct Impl;
	std::unique_ptr< Impl > m_impl;

  public:

	ModelRegistry();
	~ModelRegistry();
	ModelRegistry( const ModelRegistry& ) = delete;
	ModelRegistry& operator=( const ModelRegistry& ) = delete;

	[[nodiscard]] runtime::Task<> registerConfiguredModels();
	[[nodiscard]] runtime::Task<> reload();
	[[nodiscard]] std::vector< ModelInfo > list() const;
};
} // namespace idhan::server
