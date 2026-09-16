#pragma once

#include <chrono>
#include <memory>

#include "idhan/runtime/Task.hpp"

namespace idhan::server
{
class PeriodicServices
{
	struct Impl;
	std::unique_ptr< Impl > m_impl;

  public:

	PeriodicServices(
		std::chrono::milliseconds job_cleanup_interval,
		std::chrono::milliseconds sse_heartbeat_interval );
	~PeriodicServices();
	PeriodicServices( const PeriodicServices& ) = delete;
	PeriodicServices& operator=( const PeriodicServices& ) = delete;

	[[nodiscard]] runtime::Task<> start();
	void stopIndependentProducers() noexcept;
	[[nodiscard]] runtime::Task<> drain();
};
} // namespace idhan::server
