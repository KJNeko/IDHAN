#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>

struct JobTask;
class JobContext;

namespace idhan::jobs
{
// Tracks a service on its existing executor. The job suspends until the owner
// finishes it; it does not occupy a job-pool thread or own the service's dependencies.
class BackgroundJob
{
	class Progress;
	std::shared_ptr< Progress > m_progress;
	std::shared_ptr< JobContext > m_job;

	[[nodiscard]] static JobTask waitForStop( std::shared_ptr< Progress > progress );

  public:

	BackgroundJob(
		std::string_view name,
		std::string service,
		bool continuous = true,
		std::source_location location = std::source_location::current() );
	~BackgroundJob();
	BackgroundJob( const BackgroundJob& ) = delete;
	BackgroundJob& operator=( const BackgroundJob& ) = delete;

	void activity( std::string phase, std::string message );
	void processed( std::uint64_t count = 1 );
	void recordFailure( std::string message );
	// Call only after service work has drained. Repeated calls are harmless.
	void finish( std::optional< std::string > error = {} );
};
} // namespace idhan::jobs
