#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace idhan::search
{

enum class StepKind
{
	//! A term's query. `rows` is what the database returned.
	Fetch,
	//! A term folded into the running result. `rows` is what survived, and the step's label is the
	//! same as the fetch it corresponds to.
	Fold,
	//! The window handed back to the caller.
	Page
};

struct SearchStep
{
	std::string label {};
	std::size_t rows { 0 };
	StepKind kind { StepKind::Fetch };
	//! Wall time of the fetch, in microseconds. Zero for fold and slice steps, which are in-memory
	//! merges whose cost is already implied by the row counts on either side of them.
	std::int64_t micros { 0 };
	//! When true the row count denotes a complement, everything except these, rather than a
	//! membership list.
	bool inverted { false };
};

//! Per-step search accounting; fetches can record concurrently.
class SearchStats
{
	mutable std::mutex m_mutex {};
	std::vector< SearchStep > m_steps {};

  public:

	SearchStats() = default;
	SearchStats( const SearchStats& ) = delete;
	SearchStats& operator=( const SearchStats& ) = delete;

	void record( std::string label, std::size_t rows, StepKind kind, std::int64_t micros = 0, bool inverted = false );

	[[nodiscard]] std::vector< SearchStep > steps() const;

	//! One line per step, aligned, for the log.
	[[nodiscard]] std::string summary() const;
};

} // namespace idhan::search
