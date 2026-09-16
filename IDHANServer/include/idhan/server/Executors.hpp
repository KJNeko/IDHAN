#pragma once

#include <folly/Executor.h>
#include <folly/executors/CPUThreadPoolExecutor.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

#include "idhan/runtime/Task.hpp"

namespace idhan::server
{
enum class ExecutorState : std::uint8_t
{
	running,
	drained,
	stopped
};

class Executors
{
	std::shared_ptr< folly::CPUThreadPoolExecutor > m_application;
	std::shared_ptr< folly::CPUThreadPoolExecutor > m_heavy;
	std::shared_ptr< folly::CPUThreadPoolExecutor > m_database;
	std::size_t m_io_threads;
	std::atomic< ExecutorState > m_state { ExecutorState::running };
	std::once_flag m_drain_once;
	std::once_flag m_stop_once;

  public:

	Executors(
		std::size_t io_threads,
		std::size_t application_threads,
		std::size_t heavy_threads,
		std::size_t database_threads );
	~Executors();
	Executors( const Executors& ) = delete;
	Executors& operator=( const Executors& ) = delete;

	[[nodiscard]] folly::Executor::KeepAlive<> application() const;
	[[nodiscard]] folly::Executor::KeepAlive<> heavy() const;
	[[nodiscard]] folly::Executor::KeepAlive<> database() const;
	[[nodiscard]] std::size_t ioThreadCount() const noexcept;
	[[nodiscard]] runtime::Task<> start();
	[[nodiscard]] runtime::Task<> drain();
	void stop() noexcept;
};
} // namespace idhan::server
