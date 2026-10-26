#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "idhan/runtime/Task.hpp"

namespace idhan::server
{
class JobTask
{
	struct Impl;
	std::unique_ptr< Impl > m_impl;

  public:

	JobTask( runtime::Task<> task, std::string name );
	~JobTask();
	JobTask( JobTask&& ) noexcept;
	JobTask& operator=( JobTask&& ) noexcept;
	JobTask( const JobTask& ) = delete;
	JobTask& operator=( const JobTask& ) = delete;

	[[nodiscard]] std::string_view name() const noexcept;
	[[nodiscard]] runtime::Task<> run();
};
} // namespace idhan::server
