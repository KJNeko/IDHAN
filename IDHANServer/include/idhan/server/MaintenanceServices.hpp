#pragma once

#include <chrono>
#include <memory>

#include "idhan/runtime/Task.hpp"

namespace idhan::server
{
class MaintenanceServices
{
	struct Impl;
	std::unique_ptr< Impl > m_impl;

  public:

	MaintenanceServices( std::chrono::milliseconds interval );
	~MaintenanceServices();
	MaintenanceServices( const MaintenanceServices& ) = delete;
	MaintenanceServices& operator=( const MaintenanceServices& ) = delete;

	[[nodiscard]] runtime::Task<> start();
	void stopIndependentProducers() noexcept;
	[[nodiscard]] runtime::Task<> integrityCheck();
	[[nodiscard]] runtime::Task<> sweepHamming();
	[[nodiscard]] runtime::Task<> sweepPhash();
	[[nodiscard]] runtime::Task<> drain();
};
} // namespace idhan::server
