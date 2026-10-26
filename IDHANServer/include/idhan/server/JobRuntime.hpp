#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "JobContext.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::server
{
class Executors;

class JobRuntime
{
	struct Impl;
	std::unique_ptr< Impl > m_impl;

  public:

	JobRuntime( Executors&, runtime::DrainRegistry&, std::size_t cleanup_interval_ms );
	~JobRuntime();
	JobRuntime( const JobRuntime& ) = delete;
	JobRuntime& operator=( const JobRuntime& ) = delete;

	[[nodiscard]] runtime::Task<> start();
	[[nodiscard]] std::shared_ptr< JobContext > submit( JobTask, std::string_view name );
	[[nodiscard]] std::shared_ptr< JobContext > submitDependent(
		JobTask,
		std::string_view name,
		const JobContext& parent );
	[[nodiscard]] std::shared_ptr< JobContext > find( JobID ) const;
	[[nodiscard]] std::vector< JobSnapshot > snapshots() const;
	void stopTopLevelAdmission() noexcept;
	[[nodiscard]] runtime::Task<> drain();
};
} // namespace idhan::server
