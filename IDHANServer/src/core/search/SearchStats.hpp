//
// Created by kj16609 on 8/6/26.
//
// Row-count accounting for one search: what each term's query returned, what the running result
// looked like after each fold step, and what survived paging.
//
// The fold is the part worth seeing. A term that returns 800k rows and removes nothing from the
// result is indistinguishable from one that does all the work, unless both numbers are recorded
// next to each other.
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace idhan::search
{

//! What a step was. Carried explicitly so consumers can pair a term's fetch with the fold step that
//! folded it in, rather than recovering the relationship by parsing formatted labels.
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

//! What one step of a search produced.
struct SearchStep
{
	std::string label {};
	std::size_t rows { 0 };
	StepKind kind { StepKind::Fetch };
	//! Wall time of the fetch, in microseconds. Zero for fold and slice steps, which are in-memory
	//! merges whose cost is already implied by the row counts on either side of them.
	std::int64_t micros { 0 };
	//! When true the row count denotes a complement -- everything *except* these -- rather than a
	//! membership list. Without this a pure-exclusion search reads as though it matched almost
	//! nothing when it in fact matched almost everything.
	bool inverted { false };
};

/**
 * @brief Accumulates the per-step accounting for one search.
 *
 * Fetch steps are recorded from whichever database thread each fetch completes on, so appends are
 * locked. Everything after the fetches complete runs on the awaiting coroutine's thread alone.
 */
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
