#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <optional>

namespace idhan::hydrus
{

class ImportProgress
{
  public:

	using Clock = std::chrono::steady_clock;
	using TimePoint = Clock::time_point;

	struct Snapshot
	{
		std::size_t processed { 0 };
		std::size_t total { 0 };
		double recent_per_second { 0 };
		double average_per_second { 0 };
		std::optional< std::chrono::seconds > remaining {};
	};

	void start( std::size_t total, TimePoint now = Clock::now() );
	void advance( std::size_t count, TimePoint now = Clock::now() );
	Snapshot snapshot( TimePoint now = Clock::now() ) const;

  private:

	struct Batch
	{
		TimePoint begin;
		TimePoint end;
		std::size_t count;
	};

	std::optional< TimePoint > m_start {};
	TimePoint m_last_batch {};
	std::size_t m_total { 0 };
	std::size_t m_processed { 0 };
	std::deque< Batch > m_batches {};
};

} // namespace idhan::hydrus
