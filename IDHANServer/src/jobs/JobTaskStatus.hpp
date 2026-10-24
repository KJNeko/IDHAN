#pragma once

#include <json/value.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <source_location>
#include <string>

#include "IDHANTypes.hpp"
#include "JobProgress.hpp"

struct JobTaskStatus
{
  private:

	mutable std::mutex m_snapshot_mutex {};
	std::optional< Json::Value > m_response {};
	std::shared_ptr< const JobProgress > m_progress {};
	std::function< void() > m_on_change {};

  public:

	std::atomic< bool > m_done { false };
	std::atomic< bool > m_failed { false };
	std::string m_error_message {};
	// atomic: written by the job thread on first resume while API threads read it for running jobs
	std::atomic< std::chrono::steady_clock::time_point > m_start_time {};
	std::chrono::steady_clock::time_point m_completion_time {};
	// atomic: set by API handler threads, read by the cleanup thread
	std::atomic< bool > m_cleanup_requested { false };

	idhan::JobID m_id { 0 };
	// Set before enqueue publishes the job; immutable throughout execution and retention.
	bool m_continuous { false };

	std::string m_function_name {};
	std::source_location m_location { std::source_location::current() };

	JobTaskStatus() noexcept = default;
	void setChangeCallback( std::function< void() > callback );
	void notifyChanged() const;
	[[nodiscard]] Json::Value snapshot() const;
	void setResponse( Json::Value response );
	[[nodiscard]] std::optional< Json::Value > responseSnapshot() const;
	void setProgress( std::shared_ptr< const JobProgress > progress );
	[[nodiscard]] std::optional< Json::Value > progressSnapshot() const;
};
