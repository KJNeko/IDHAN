#pragma once

#include <folly/Executor.h>
#include <folly/File.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "ModulePool.hpp"

namespace idhan::server::modules
{

struct ModuleLibraryConfig
{
	std::filesystem::path library;
	ModulePoolConfig pool;
};

struct ModuleRuntimeConfig
{
	std::filesystem::path runner_path;
	std::vector< ModuleLibraryConfig > libraries;
	std::uint32_t callback_depth_limit { 8 };
};

class ModuleRuntime
{
	struct Impl;
	std::unique_ptr< Impl > m_impl;

  public:

	ModuleRuntime( ModuleRuntimeConfig, folly::Executor::KeepAlive<> );
	~ModuleRuntime();
	ModuleRuntime( ModuleRuntime&& ) noexcept;
	ModuleRuntime& operator=( ModuleRuntime&& ) noexcept;
	ModuleRuntime( const ModuleRuntime& ) = delete;
	ModuleRuntime& operator=( const ModuleRuntime& ) = delete;

	[[nodiscard]] runtime::Task<> start();
	[[nodiscard]] runtime::Task< WorkerResult > dispatch( std::size_t library_index, WorkerCall );
	void setCallbackHandler( CallbackHandler );
	[[nodiscard]] runtime::Task<> maintain();
	[[nodiscard]] runtime::Task<> drain();
	[[nodiscard]] std::vector< ipc::binary::ManifestEntry > manifest() const;
	[[nodiscard]] std::vector< std::string > librarySignatures() const;
};

} // namespace idhan::server::modules
