#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>

#include "idhan/runtime/DrainRegistry.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::server
{
enum class ShutdownState : std::uint8_t
{
	running,
	draining,
	forced,
	stopped
};

class ShutdownController
{
	struct Impl;
	std::unique_ptr< Impl > m_impl;

  public:

	ShutdownController( runtime::DrainRegistry&, std::chrono::milliseconds );
	~ShutdownController();
	ShutdownController( const ShutdownController& ) = delete;
	ShutdownController& operator=( const ShutdownController& ) = delete;

	[[nodiscard]] ShutdownState state() const noexcept;
	[[nodiscard]] bool accepting() const noexcept;
	void requestShutdown() noexcept;
	void forceShutdown() noexcept;
	[[nodiscard]] runtime::Task<> drain();
	[[nodiscard]] runtime::Task<> installSignalHandlers();
	void removeSignalHandlers() noexcept;
};
} // namespace idhan::server
