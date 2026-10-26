#pragma once

#include <folly/File.h>

#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

#include "idhan/ipc/binary/Frame.hpp"
#include "idhan/ipc/binary/Messages.hpp"
#include "idhan/ipc/binary/ModuleRunner.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::server::modules
{

struct WorkerConfig
{
	std::filesystem::path runner_path;
	std::filesystem::path library_path;
	std::uint32_t max_payload { 64U * 1024U * 1024U };
	std::chrono::milliseconds startup_timeout { std::chrono::seconds( 30 ) };
	std::chrono::milliseconds heartbeat_timeout { std::chrono::seconds( 30 ) };
	std::chrono::milliseconds idle_recycle { std::chrono::minutes( 10 ) };
	std::uint64_t rss_ceiling_mb { 0 };
	std::uint32_t nested_depth_limit { 8 };
};

enum class WorkerState
{
	created,
	starting,
	ready,
	draining,
	stopped,
	failed
};

struct WorkerCall
{
	ipc::binary::Call call;
	std::vector< folly::File > descriptors;
};

using WorkerResult = ipc::binary::OwnedResult;
using CallbackResult = ipc::binary::OwnedCallbackResult;
using CallbackHandler = ipc::binary::RunnerCallback;

class ModuleWorker
{
	struct Impl;
	std::unique_ptr< Impl > m_impl;

  public:

	ModuleWorker( WorkerConfig, std::uint64_t generation );
	~ModuleWorker();
	ModuleWorker( ModuleWorker&& ) noexcept;
	ModuleWorker& operator=( ModuleWorker&& ) noexcept;
	ModuleWorker( const ModuleWorker& ) = delete;
	ModuleWorker& operator=( const ModuleWorker& ) = delete;

	[[nodiscard]] runtime::Task<> start();
	[[nodiscard]] runtime::Task< WorkerResult > dispatch( WorkerCall );
	void setCallbackHandler( CallbackHandler );
	[[nodiscard]] runtime::Task<> reclaim();
	[[nodiscard]] runtime::Task<> drain();
	void terminate();

	[[nodiscard]] WorkerState state() const noexcept;
	[[nodiscard]] std::uint64_t generation() const noexcept;
	[[nodiscard]] const ipc::binary::Manifest& manifest() const;
};

using WorkerProcess = ModuleWorker;

} // namespace idhan::server::modules
