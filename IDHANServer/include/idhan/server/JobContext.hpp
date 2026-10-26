#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "IDHANTypes.hpp"
#include "idhan/runtime/DrainRegistry.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::server
{
enum class JobState : std::uint8_t
{
	queued,
	running,
	succeeded,
	failed
};

struct JobSnapshot
{
	JobID id { 0 };
	std::string name;
	JobState state { JobState::queued };
	std::optional< std::string > error;
	std::chrono::system_clock::time_point created;
	std::optional< std::chrono::system_clock::time_point > started;
	std::optional< std::chrono::system_clock::time_point > completed;
};

class JobTask;
class JobRuntime;

class JobContext
{
	struct Impl;
	std::shared_ptr< Impl > m_impl;
	friend class JobRuntime;

	JobContext( std::shared_ptr< Impl > );

  public:

	~JobContext();
	JobContext( const JobContext& ) = delete;
	JobContext& operator=( const JobContext& ) = delete;

	[[nodiscard]] JobID id() const noexcept;
	[[nodiscard]] JobSnapshot snapshot() const;
	[[nodiscard]] runtime::Task<> wait();
	[[nodiscard]] runtime::WorkRegistration childWork() const;
};
} // namespace idhan::server
