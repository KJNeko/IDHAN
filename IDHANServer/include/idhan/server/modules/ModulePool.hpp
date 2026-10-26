#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "ModuleWorker.hpp"

namespace idhan::server::modules
{

struct ModulePoolConfig
{
	WorkerConfig worker;
	std::size_t workers { 1 };
};

class ModulePool
{
	struct Impl;
	std::unique_ptr< Impl > m_impl;

  public:

	ModulePool( ModulePoolConfig, std::uint64_t pool_generation );
	~ModulePool();
	ModulePool( ModulePool&& ) noexcept;
	ModulePool& operator=( ModulePool&& ) noexcept;
	ModulePool( const ModulePool& ) = delete;
	ModulePool& operator=( const ModulePool& ) = delete;

	[[nodiscard]] runtime::Task<> start();
	[[nodiscard]] runtime::Task< WorkerResult > dispatch( WorkerCall );
	void setCallbackHandler( CallbackHandler );
	[[nodiscard]] runtime::Task<> maintain();
	[[nodiscard]] runtime::Task<> drain();
	[[nodiscard]] std::vector< ipc::binary::ManifestEntry > manifest() const;
};

} // namespace idhan::server::modules
